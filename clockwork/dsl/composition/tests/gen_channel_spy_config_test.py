# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for channel spy configs generation."""

from pathlib import Path

import pytest
from clockwork.dsl.composition import (
    channel_spy_config,
    channel_spy_config_proto,
    gen_channel_spy_configs,
    system,
)
from clockwork.dsl.ir import box, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import protocol


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_gen_configs(tmp_path: Path, fs_importer: FilesystemImporter) -> None:
    def test_serialization(config: channel_spy_config_proto.ChannelSpyConfig) -> None:
        # Test serialization/deserialization
        buffer = bytearray(channel_spy_config.ChannelSpyConfig.get_tachyon_constraint().size)
        config.serialize_tachyon(memoryview(buffer))
        config_from_serdes = channel_spy_config.ChannelSpyConfig.deserialize_tachyon(memoryview(buffer))
        assert config == config_from_serdes
        # Test read/write file
        tmp_file = tmp_path / "ChannelSpyConfig.tachyon"
        protocol.write_tachyon_to_file(config, tmp_file)
        config_from_file = protocol.read_tachyon_from_file(channel_spy_config.ChannelSpyConfig, tmp_file)
        assert config == config_from_file

    # Generate configs
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False, False)
    physical_system = system.make_physical_system(logical_system)
    config_by_domain = gen_channel_spy_configs.gen_channel_spy_configs(module.context, physical_system)
    assert len(config_by_domain.values()) == 2

    for config in config_by_domain.values():
        test_serialization(config)
