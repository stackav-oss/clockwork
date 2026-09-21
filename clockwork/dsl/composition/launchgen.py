# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

import os
from pathlib import Path
from uuid import UUID

from clockwork.dsl.composition import (
    pdf,
    platform_diagnostics_config,
    simplelaunch_runner_config,
    simplelaunch_runner_config_proto,
    system,
)
from clockwork.dsl.composition.channel_config import ChannelType
from clockwork.dsl.composition.str_manip import snake_from_camel
from clockwork.dsl.ir import uuid_reg
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from google.protobuf import text_format
from jewels.simplelaunch.v1.config_pb2 import AppConfig, Config

_PINION_TCP_BRIDGE_PATH = Path("clockwork/pinion/tcp_bridge_main")


def _make_path_from_fqn(fqn: str) -> Path:
    """Make a path from a fully qualified name.

    This strips the leading colons from a fully qualified name and constructs a path
    by replacing '::' with '/' and removing the repo name.

    Args:
        fqn: Fully qualified name.

    Returns:
        Generated path.
    """
    if "::" in fqn:
        module_str, value_name_str = fqn.split(".", 1)
        module_id = ModuleID.from_fqn(module_str)
        return BazelPathResolver().to_runtime_path(module_id).with_suffix(f".{value_name_str}")
    # TODO(OI-3052): When logical systems are built, they dynamically
    # generate systems in the current working directory dynamically,
    # but the fqn does not include a module name thus we cannot use
    # the module ID tooling.
    return Path(fqn)


def gen_process_config(  # noqa: PLR0913 (mitigated by kwonly args)
    *,
    exe_fqn: str,
    sys_fqn: str,
    config_file: str,
    config: Config,
    proc_name: str,
    cpus: list[int],
    env: dict[str, str] | None = None,
) -> None:
    """Add a simplelaunch configuration for an executable to a config instance.

    Args:
        exe_fqn: Executable fully qualified domain name.
        sys_fqn: System fully qualified domain name.
        config_file: Process configuration file name.
        config: Configuration
        proc_name: Fully-qualified name of the proc.
        cpus: CPU IDs the process runs on
        env: Environment variables to set for the process.
    """
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    launch_name = str(proc_name).split(".")[-1]
    exe_path = _make_path_from_fqn(exe_fqn)
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    exe_name = str(exe_path.name).split(".")[-1]
    sys_path = _make_path_from_fqn(sys_fqn)
    config.app.append(
        AppConfig(
            name=launch_name,
            executable=os.fspath(exe_path.parent / exe_name),
            args=[
                os.fspath(sys_path.parent / config_file),
            ],
            cpus=cpus,
            env=env or {},
        )
    )


def gen_simplelaunch_runner_config_path(
    system_fqn: str, phys_domain: system.PhysicalCpuDomain, runner_config_file: str
) -> str:
    """Generate the simplelaunch runner config path used in the simplelaunch config textproto.

    Args:
        system_fqn: System fully qualified name.
        phys_domain: Physical system CPU domain.
        runner_config_file: Simple runner config file name.

    Return:
        Runtime path to the simplelaunch runner config file.
    """
    runner_config_fqn = f"{system_fqn}.{phys_domain.logical.name}_{runner_config_file}"
    return str(_make_path_from_fqn(runner_config_fqn))


def gen_simplelaunch_runner_configs(
    physical_system: system.PhysicalSystem,
) -> dict[UUID, simplelaunch_runner_config_proto.SimplelaunchRunnerConfig]:
    """Generate per-domain simplelaunch runner configuration files for a system."""
    result = {}
    if not physical_system.system.use_simplelaunch:
        return result
    for domain_uuid, domain in physical_system.cpu_domains.items():
        simplelaunch_runner_uuid = uuid_reg.uuid_from_name(f"{domain.logical.value_key()}.__SIMPLELAUNCH_RUNNER__")
        config = simplelaunch_runner_config.SimplelaunchRunnerConfig(
            host_name=snake_from_camel(domain.logical.name),
            diagnostics_config=platform_diagnostics_config.PlatformDiagnosticsConfig(
                reporter_id=UUID(int=0),
                group_id="",
                instance_id="",
                publish_endpoint=pdf.PublishEndpoint(
                    process_id=UUID(int=0),
                    publisher_id=UUID(int=0),
                    buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0, is_published_once=False),
                    num_subscribers=0,
                    channel_name="",
                    is_bulk_data=False,
                    channel_type=ChannelType.unspecified,
                ),
            ),
            status_publish_endpoint=pdf.PublishEndpoint(
                process_id=UUID(int=0),
                publisher_id=UUID(int=0),
                buffer_layout=pdf.PinionBufferLayout(num_slots=0, message_size=0, is_published_once=False),
                num_subscribers=0,
                channel_name="",
                is_bulk_data=False,
                channel_type=ChannelType.unspecified,
            ),
        )

        if domain.simplelaunch_diagnostics_producer:
            diagnostics_producer = domain.platform_diagnostics_producers[domain.simplelaunch_diagnostics_producer]
            diagnostics_buffer = domain.buffers[diagnostics_producer.pinion_buffer]
            config.diagnostics_config.reporter_id = diagnostics_producer.uuid
            config.diagnostics_config.group_id = diagnostics_producer.group_id
            config.diagnostics_config.instance_id = diagnostics_producer.instance_id
            config.diagnostics_config.publish_endpoint = pdf.PublishEndpoint(
                process_id=simplelaunch_runner_uuid,
                publisher_id=diagnostics_buffer.uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=diagnostics_buffer.layout.num_slots,
                    message_size=diagnostics_buffer.layout.message_size,
                    is_published_once=diagnostics_buffer.layout.is_published_once,
                    max_msgs_per_exec=diagnostics_buffer.layout.max_msgs_per_exec,
                ),
                num_subscribers=diagnostics_buffer.num_subscribers,
                channel_name=diagnostics_buffer.channel.channel.channel_name,
                is_bulk_data=diagnostics_buffer.channel.is_bulk_data(),
                channel_type=diagnostics_buffer.channel.channel_type(),
            )

        if domain.simplelaunch_status_producer:
            simplelaunch_status_producer = domain.platform_status_producers[domain.simplelaunch_status_producer]
            simplelaunch_status_buffer = domain.buffers[simplelaunch_status_producer.pinion_buffer]
            config.status_publish_endpoint = pdf.PublishEndpoint(
                process_id=simplelaunch_runner_uuid,
                publisher_id=simplelaunch_status_buffer.uuid,
                buffer_layout=pdf.PinionBufferLayout(
                    num_slots=simplelaunch_status_buffer.layout.num_slots,
                    message_size=simplelaunch_status_buffer.layout.message_size,
                    is_published_once=simplelaunch_status_buffer.layout.is_published_once,
                    max_msgs_per_exec=simplelaunch_status_buffer.layout.max_msgs_per_exec,
                ),
                num_subscribers=simplelaunch_status_buffer.num_subscribers,
                channel_name=simplelaunch_status_buffer.channel.channel.channel_name,
                is_bulk_data=simplelaunch_status_buffer.channel.is_bulk_data(),
                channel_type=simplelaunch_status_buffer.channel.channel_type(),
            )

        result[domain_uuid] = config

    return result


def gen_tcp_bridge_config(
    *,
    sys_fqn: str,
    config_file: str,
    config: Config,
    cpus: list[int],
) -> None:
    """Add a simplelaunch configuration for an executable to a config instance.

    Args:
        sys_fqn: System fully qualified domain name.
        config_file: Process configuration file name.
        config: Configuration
        cpus: CPU IDs the bridge runs on
    """
    sys_path = _make_path_from_fqn(sys_fqn)
    config.app.append(
        AppConfig(
            name=_PINION_TCP_BRIDGE_PATH.name,
            executable=os.fspath(_PINION_TCP_BRIDGE_PATH),
            args=[
                os.fspath(sys_path.parent / config_file),
            ],
            cpus=cpus,
        )
    )


def write_simplelaunch_config_to_file(config: Config, filename: Path, mode: str = "w") -> None:
    """Writes a simplelaunch configuration to a file.

    Args:
        config: Simplelaunch configuration to write.
        filename: Output file name.
        mode: Output file mode.
    """
    with filename.open(mode) as output:
        output.write(text_format.MessageToString(config))
