# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Generate logger configurations."""

import os
from pathlib import Path

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
    launch_name = str(proc_name).split(".")[-1]
    exe_path = _make_path_from_fqn(exe_fqn)
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
