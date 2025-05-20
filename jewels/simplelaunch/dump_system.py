# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Dump the Simplelaunch system configuration to a file."""

from pathlib import Path

import click


@click.command()
@click.argument("output", type=click.Path(path_type=Path))
@click.argument("node_configs", nargs=-1)
def dump_system(output: Path, node_configs: tuple[str]) -> None:
    """Dump the system description for the given nodes and configs."""
    with output.open("w") as o:
        for node_config in node_configs:
            node, config = node_config.split("=")
            with Path(config).open() as config_file:
                name = None
                for raw_line in config_file:
                    line = raw_line.strip()
                    if line.startswith("name: "):
                        name = line.removeprefix("name: ").strip('"')
                    if line.startswith("executable: "):
                        line = line.removeprefix("executable: ").strip('"')
                        o.write(f"{node}: {name}: {line}\n")
                        name = None


if __name__ == "__main__":
    dump_system()
