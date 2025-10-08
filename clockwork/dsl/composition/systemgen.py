# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate ProcessDescriptions from Process IR."""

from __future__ import annotations

import json
from csv import DictWriter
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any, TypeAlias, cast
from uuid import UUID

from clockwork.dsl.bazel.simple_launch_targets import MergeSimplelaunchConfig, config_name_from_domain_node_name
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.composition import (
    bridgegen,
    gen_channel_allocation_report,
    gen_channel_spy_configs,
    gen_diagnostics_configs,
    gen_logger_configs,
    gen_metrics_channel_metadata_configs,
    gen_multi_subscriber_configs,
    genpd,
    launchgen,
    logger_config,
    pdf,
    system,
)
from clockwork.dsl.ir.module_id import CLK_REPO
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.serialization.py.protocol import write_tachyon_to_file
from jewels.simplelaunch.v1.config_pb2 import Config
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Iterator

    from clockwork.dsl.ir import system_target
    from clockwork.dsl.ir.hardware import CpuDomain


@dataclass(slots=True)
class GeneratedSystemFiles:
    """Return structure for gen_system."""

    process_description_files: list[Path]
    telemetry_logger_config_files: list[Path]
    event_logger_config_files: list[Path]
    bridge_config_files: list[Path]
    simplelaunch_config_files: list[Path]
    multi_subscriber_config_files: list[Path]
    channel_publisher_config_files: list[Path]
    channel_allocation_report_files: list[Path]
    channel_spy_config_files: list[Path]
    diagnostics_database_config_files: list[Path]
    logged_channel_metadata_files: list[Path]
    metrics_channel_metadata_files: list[Path]

    def all_files(self) -> Iterator[Path]:
        """Yield all files of all types."""
        yield from self.process_description_files
        yield from self.telemetry_logger_config_files
        yield from self.event_logger_config_files
        yield from self.bridge_config_files
        yield from self.simplelaunch_config_files
        yield from self.multi_subscriber_config_files
        yield from self.channel_publisher_config_files
        yield from self.channel_allocation_report_files
        yield from self.channel_spy_config_files
        yield from self.diagnostics_database_config_files
        yield from self.logged_channel_metadata_files
        yield from self.metrics_channel_metadata_files


@dataclass
class DomainOutputTargets:
    """A cpu domain and its output targets."""

    domain: CpuDomain
    simple_launch_config: MergeSimplelaunchConfig


OutputTargetsByDomain: TypeAlias = dict[UUID, DomainOutputTargets]


def make_filename_from_value_key(value_key: str) -> str:
    """Remove/convert special characters in a value key to make a reasonable filename.

    This is intended to turn a single value key into a single file name, not
    including any path/subdirectory components.  This creates "flat",
    single-directory naming system for all of the data files that describe a
    single system, while keeping the filenames unique.  Leading colons
    representing the builtins root namespace (e.g., `::Tachyon`) are stripped
    and other `::` instances are replaced with `.`
    """
    return value_key.lstrip(":").replace("::", ".").replace("@", "")


class PdfJsonEncoder(json.JSONEncoder):
    """Encoder for non-JSON types in PDF."""

    @override
    def default(self, o: Any) -> Any:
        """Default encoder."""
        enum_types = (
            pdf.MemoryResourceType,
            logger_config.MessageEncoding,
            logger_config.SchemaEncoding,
            logger_config.ChannelType,
            pdf.NotConnectedEndpointType,
        )
        if isinstance(o, UUID):
            # if the obj is uuid, we simply return the value of uuid
            return o.hex
        if isinstance(o, enum_types):
            return o.value  # pyright: ignore[reportAttributeAccessIssue] False positive
        return json.JSONEncoder.default(self, o)


def gen_system(
    root_dir: Path, system_target_ir: system_target.SystemTarget, write_files: bool, write_json_files: bool
) -> tuple[GeneratedSystemFiles, OutputTargetsByDomain, system.PhysicalSystem]:
    """Generate system description files for a system target."""
    include_dir = BazelPathResolver().to_buildtime_path(system_target_ir.module.module_id).parent
    logical_system = system.make_system(
        [system_target_ir.box_instance], system_target_ir.module, system_target_ir.require_logging_policies
    )
    return gen_system_from_logical_system(
        include_dir=include_dir,
        root_dir=root_dir,
        logical_system=logical_system,
        write_files=write_files,
        write_json_files=write_json_files,
        system_fqn=system_target_ir.fqn,
        system_key=system_target_ir.value_key(),
    )


def _get_name_from_system_key(system_key: str, domain_name: str) -> str:
    """Generate a name for config files from system key and domain name."""
    target_path, target_prefix, target_name = make_filename_from_value_key(system_key).rsplit(".", 2)  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    return f"{target_prefix}.{target_name}.{domain_name}"


# TODO(OI-3143): Correctly resolve labels so that they are not external when inside the clockwork module.
def _initialize_output_targets(
    *,
    logical_system: system.LogicalSystem,
) -> OutputTargetsByDomain:
    """Initialize the output targets for each domain."""
    output_targets: OutputTargetsByDomain = {}
    for domain_uuid, domain in logical_system.cpu_domains.items():
        domain_srcs: list[Label] = [
            Label(f"@{CLK_REPO}//clockwork/launch:clockwork_prelaunch.textproto"),
        ]
        for label in domain.simplelaunch_srcs:
            assert isinstance(label, str)
            domain_srcs.append(Label(label))
        output_targets[domain_uuid] = DomainOutputTargets(
            domain,
            MergeSimplelaunchConfig(
                name=config_name_from_domain_node_name(domain.simplelaunch_node_name()),
                srcs=domain_srcs,
                data=[Label(f"@{CLK_REPO}//clockwork/pinion:tcp_bridge_main")],
            ),
        )
    return output_targets


def _generate_process_descriptions(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    logical_system: system.LogicalSystem,
    process_descs: dict[UUID, Any],
    output_targets: OutputTargetsByDomain,
    simplelaunch_configs: dict[UUID, Config],
    system_fqn: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate process description files."""
    result_paths: list[Path] = []
    for process_uuid, process_desc in process_descs.items():
        process = logical_system.processes[process_uuid]
        name = make_filename_from_value_key(process.value_key())
        file_name = Path(f"{name}.tachyon")
        local_path = include_dir / file_name
        pd_path = root_dir / local_path
        result_paths.append(pd_path)

        cpu_domain = logical_system.process_to_domain[process_uuid]
        phys_domain = logical_system.cpu_domains[cpu_domain]
        launchgen.gen_process_config(
            exe_fqn=process.executable.fqn,
            sys_fqn=system_fqn,
            config_file=pd_path.name,
            config=simplelaunch_configs[cpu_domain],
            proc_name=name,
            cpus=phys_domain.default_cpus,
        )

        cast("list[Path | Label]", output_targets[cpu_domain].simple_launch_config.data).append(file_name)

        executable_repo_prefix = (
            ""
            if process.executable.module.module_id.repo == logical_system.module.module_id.repo
            else f"@{process.executable.module.module_id.repo}"
        )

        executable_label = Label(
            f"{executable_repo_prefix}//{process.executable.module.module_id.get_base_path().parent}:{process.executable.name}"
        )
        cast("list[Path | Label]", output_targets[cpu_domain].simple_launch_config.data).append(executable_label)

        if write_files:
            write_tachyon_to_file(process_desc, pd_path)
            if write_json_files:
                json_path = pd_path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(process_desc), json_file, indent=2, cls=PdfJsonEncoder)

    return result_paths


def _generate_logger_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> tuple[list[Path], list[Path], list[Path], list[Path]]:
    """Generate logger configuration files."""
    event_logger_files: list[Path] = []
    telemetry_logger_files: list[Path] = []
    log_reader_files: list[Path] = []
    logged_channel_metadata_files: list[Path] = []

    # Generate logger configs
    logger_configs_by_domain = gen_logger_configs.gen_logger_configs(physical_system)
    for domain_uuid, logger_configs in logger_configs_by_domain.items():
        phys_domain = physical_system.cpu_domains[domain_uuid]
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)

        event_file_name = Path(f"{name}_event_logger_config.tachyon")
        event_local_path = include_dir / event_file_name
        event_path = root_dir / event_local_path

        telemetry_file_name = Path(f"{name}_telemetry_logger_config.tachyon")
        telemetry_local_path = include_dir / telemetry_file_name
        telemetry_path = root_dir / telemetry_local_path

        event_logger_files.append(event_path)
        telemetry_logger_files.append(telemetry_path)

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(event_file_name)
        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(telemetry_file_name)

        if write_files:
            write_tachyon_to_file(
                obj=logger_configs.events_config,
                filename=event_path,
            )
            write_tachyon_to_file(
                obj=logger_configs.telemetry_config,
                filename=telemetry_path,
            )
            if write_json_files:
                event_json_path = event_path.with_suffix(".json")
                with event_json_path.open("w", encoding="utf-8") as event_json_file:
                    json.dump(asdict(obj=logger_configs.events_config), event_json_file, indent=2, cls=PdfJsonEncoder)
                telemetry_json_path = telemetry_path.with_suffix(".json")
                with telemetry_json_path.open("w", encoding="utf-8") as telemetry_json_file:
                    json.dump(
                        asdict(obj=logger_configs.telemetry_config), telemetry_json_file, indent=2, cls=PdfJsonEncoder
                    )

    # Generate logged channel metadata
    logged_channel_metadata = gen_logger_configs.gen_logged_channel_metadata(physical_system)
    _, target_prefix, target_name = make_filename_from_value_key(system_key).rsplit(".", 2)
    logged_channel_metadata_file_name = f"{target_prefix}.{target_name}.logged_channel_metadata.pbbin"
    logged_channel_metadata_local_path = include_dir / logged_channel_metadata_file_name
    logged_channel_metadata_path = root_dir / logged_channel_metadata_local_path
    logged_channel_metadata_files.append(logged_channel_metadata_path)
    if write_files:
        with logged_channel_metadata_path.open("wb") as logged_channel_metadata_file:
            logged_channel_metadata_file.write(logged_channel_metadata.SerializeToString())

    # Generate channel publisher configs
    for domain_uuid, channel_publisher_config in gen_logger_configs.gen_channel_publisher_configs(
        physical_system
    ).items():
        phys_domain = physical_system.cpu_domains[domain_uuid]
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_channel_publisher_config.tachyon")
        local_path = include_dir / file_name
        path = root_dir / local_path
        log_reader_files.append(path)

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(file_name)

        if write_files:
            write_tachyon_to_file(
                obj=channel_publisher_config,
                filename=path,
            )
            if write_json_files:
                json_path = path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(obj=channel_publisher_config), json_file, indent=2, cls=PdfJsonEncoder)

    return event_logger_files, telemetry_logger_files, log_reader_files, logged_channel_metadata_files


def _generate_bridge_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    simplelaunch_configs: dict[UUID, Config],
    system_fqn: str,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate bridge configuration files."""
    bridge_files: list[Path] = []

    for domain_uuid, bridge_config in bridgegen.gen_bridge_config(physical_system).items():
        phys_domain = physical_system.cpu_domains[domain_uuid]
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_bridge_config.tachyon")
        local_path = include_dir / file_name
        path = root_dir / local_path
        bridge_files.append(path)
        launchgen.gen_bridge_config(
            sys_fqn=system_fqn,
            config_file=local_path.name,
            config=simplelaunch_configs[domain_uuid],
            cpus=phys_domain.logical.bridge_cpus,
        )

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(file_name)

        if write_files:
            write_tachyon_to_file(obj=bridge_config, filename=path)
            if write_json_files:
                json_path = path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(obj=bridge_config), json_file, indent=2, cls=PdfJsonEncoder)

    return bridge_files


def _generate_simplelaunch_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    simplelaunch_configs: dict[UUID, Config],
    system_key: str,
    write_files: bool,
) -> list[Path]:
    """Generate simplelaunch configuration files."""
    simplelaunch_files: list[Path] = []

    for domain_uuid, phys_domain in physical_system.cpu_domains.items():
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_simplelaunch_config.textproto")
        local_path = include_dir / file_name
        path = root_dir / local_path
        simplelaunch_files.append(path)
        config = simplelaunch_configs[domain_uuid]

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.srcs).append(file_name)

        if write_files:
            launchgen.write_simplelaunch_config_to_file(config, path)

    return simplelaunch_files


def _generate_multi_subscriber_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate multi-subscriber configuration files."""
    multi_subscriber_files: list[Path] = []

    for cpu_uuid, cpu_config in gen_multi_subscriber_configs.gen_multi_subscriber_configs(physical_system).items():
        cpu_name = physical_system.cpu_domains[cpu_uuid].logical.name
        target_path, target_prefix, target_name = make_filename_from_value_key(system_key).rsplit(".", 2)  # pyright: ignore[reportUnusedVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

        for channel_name, multi_subscriber_config in cpu_config.items():
            name = f"{target_prefix}.{target_name}.{cpu_name}.{channel_name}"
            file_name = Path(f"{name}_config.tachyon")
            local_path = include_dir / file_name
            path = root_dir / local_path
            multi_subscriber_files.append(path)

            cast("list[Path | Label]", output_targets[cpu_uuid].simple_launch_config.data).append(file_name)

            if write_files:
                write_tachyon_to_file(obj=multi_subscriber_config, filename=path)
                if write_json_files:
                    json_path = root_dir / include_dir / f"{name}_config.json"
                    with json_path.open("w", encoding="utf-8") as json_file:
                        json.dump(asdict(obj=multi_subscriber_config), json_file, indent=2, cls=PdfJsonEncoder)

    return multi_subscriber_files


def _generate_diagnostics_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate diagnostics configuration files."""
    diagnostics_files: list[Path] = []

    diagnostics_config = gen_diagnostics_configs.gen_diagnostics_configs(physical_system)
    if diagnostics_config.reporters:
        _, target_prefix, target_name = make_filename_from_value_key(system_key).rsplit(".", 2)
        name = f"{target_prefix}.{target_name}.diagnostics_database"
        file_name = Path(f"{name}_config.tachyon")
        local_path = include_dir / file_name
        path = root_dir / local_path
        diagnostics_files.append(path)
        for cpu_uuid in physical_system.cpu_domains:
            cast("list[Path | Label]", output_targets[cpu_uuid].simple_launch_config.data).append(file_name)
        if write_files:
            write_tachyon_to_file(obj=diagnostics_config, filename=path)
            if write_json_files:
                json_path = path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(diagnostics_config), json_file, indent=2, cls=PdfJsonEncoder)

    return diagnostics_files


def _generate_channel_spy_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    logical_system: system.LogicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate channel spy configuration files."""
    channel_spy_files: list[Path] = []

    channel_spy_config_by_domain = gen_channel_spy_configs.gen_channel_spy_configs(
        logical_system.module.context, physical_system
    )
    for domain_uuid, channel_spy_config in channel_spy_config_by_domain.items():
        phys_domain = physical_system.cpu_domains[domain_uuid]
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_channel_spy_config.tachyon")
        local_path = include_dir / file_name
        path = root_dir / local_path
        channel_spy_files.append(path)

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(file_name)

        if write_files:
            write_tachyon_to_file(
                obj=channel_spy_config,
                filename=path,
            )
            if write_json_files:
                json_path = path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(obj=channel_spy_config), json_file, indent=2, cls=PdfJsonEncoder)

    return channel_spy_files


def _generate_metrics_channel_metadata_configs(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
    write_json_files: bool,
) -> list[Path]:
    """Generate channel spy configuration files."""
    metrics_channel_metadata_files: list[Path] = []

    metrics_channel_metadata_config_by_domain = (
        gen_metrics_channel_metadata_configs.gen_metrics_channel_metadata_configs(physical_system)
    )
    for domain_uuid, metrics_channel_metadata_config in metrics_channel_metadata_config_by_domain.items():
        phys_domain = physical_system.cpu_domains[domain_uuid]
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_metrics_channel_metadata_config.tachyon")
        local_path = include_dir / file_name
        path = root_dir / local_path
        metrics_channel_metadata_files.append(path)

        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(file_name)

        if write_files:
            write_tachyon_to_file(
                obj=metrics_channel_metadata_config,
                filename=path,
            )
            if write_json_files:
                json_path = path.with_suffix(".json")
                with json_path.open("w", encoding="utf-8") as json_file:
                    json.dump(asdict(obj=metrics_channel_metadata_config), json_file, indent=2, cls=PdfJsonEncoder)

    return metrics_channel_metadata_files


def _generate_channel_allocation_reports(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    physical_system: system.PhysicalSystem,
    output_targets: OutputTargetsByDomain,
    system_key: str,
    write_files: bool,
) -> list[Path]:
    """Generate channel allocation report files."""
    channel_allocation_files: list[Path] = []

    for domain_uuid, phys_domain in physical_system.cpu_domains.items():
        name = _get_name_from_system_key(system_key, phys_domain.logical.name)
        file_name = Path(f"{name}_channel_allocations.csv")
        local_path = include_dir / file_name
        path = root_dir / local_path
        fieldnames, rows = gen_channel_allocation_report.gen_channel_allocation_report(phys_domain)
        channel_allocation_files.append(path)
        cast("list[Path | Label]", output_targets[domain_uuid].simple_launch_config.data).append(file_name)

        if write_files:
            with path.open("w", newline="") as csvfile:
                writer = DictWriter(csvfile, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerows(rows)

    return channel_allocation_files


def gen_system_from_logical_system(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    include_dir: Path,
    root_dir: Path,
    logical_system: system.LogicalSystem,
    write_files: bool,
    write_json_files: bool,
    system_fqn: str,
    system_key: str,
) -> tuple[GeneratedSystemFiles, OutputTargetsByDomain, system.PhysicalSystem]:
    """Generate system description files for a logical system."""
    physical_system = system.make_physical_system(logical_system)
    system.add_metrics_logging_observers(physical_system)
    simplelaunch_configs: dict[UUID, Config] = {domain_uuid: Config() for domain_uuid in logical_system.cpu_domains}
    result = GeneratedSystemFiles([], [], [], [], [], [], [], [], [], [], [], [])
    process_descs = genpd.gen_pd_sys(physical_system)

    # Initialize output targets
    output_targets = _initialize_output_targets(logical_system=logical_system)

    # Generate process descriptions
    result.process_description_files = _generate_process_descriptions(
        include_dir=include_dir,
        root_dir=root_dir,
        logical_system=logical_system,
        process_descs=process_descs,
        output_targets=output_targets,
        simplelaunch_configs=simplelaunch_configs,
        system_fqn=system_fqn,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate logger configurations
    (
        result.event_logger_config_files,
        result.telemetry_logger_config_files,
        result.channel_publisher_config_files,
        result.logged_channel_metadata_files,
    ) = _generate_logger_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate bridge configurations
    result.bridge_config_files = _generate_bridge_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        simplelaunch_configs=simplelaunch_configs,
        system_fqn=system_fqn,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate simplelaunch configurations
    result.simplelaunch_config_files = _generate_simplelaunch_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        simplelaunch_configs=simplelaunch_configs,
        system_key=system_key,
        write_files=write_files,
    )

    # Generate multi-subscriber configurations
    result.multi_subscriber_config_files = _generate_multi_subscriber_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate diagnostics configurations
    result.diagnostics_database_config_files = _generate_diagnostics_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate channel spy configurations
    result.channel_spy_config_files = _generate_channel_spy_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        logical_system=logical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    # Generate channel allocation reports
    result.channel_allocation_report_files = _generate_channel_allocation_reports(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
    )

    # Generate metrics channel metadata configurations
    result.metrics_channel_metadata_files = _generate_metrics_channel_metadata_configs(
        include_dir=include_dir,
        root_dir=root_dir,
        physical_system=physical_system,
        output_targets=output_targets,
        system_key=system_key,
        write_files=write_files,
        write_json_files=write_json_files,
    )

    return result, output_targets, physical_system
