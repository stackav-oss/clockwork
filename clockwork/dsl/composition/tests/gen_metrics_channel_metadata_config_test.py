# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for channel spy configs generation."""

from pathlib import Path

import pytest
from clockwork.dsl.composition import (
    constants,
    gen_metrics_channel_metadata_configs,
    metrics_channel_metadata_config,
    metrics_channel_metadata_config_proto,
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
    def test_serialization(config: metrics_channel_metadata_config_proto.MetricsChannelMetadataConfig) -> None:
        # Test serialization/deserialization
        buffer = bytearray(metrics_channel_metadata_config.MetricsChannelMetadataConfig.get_tachyon_constraint().size)
        config.serialize_tachyon(memoryview(buffer))
        config_from_serdes = metrics_channel_metadata_config.MetricsChannelMetadataConfig.deserialize_tachyon(
            memoryview(buffer)
        )
        assert config == config_from_serdes
        # Test read/write file
        tmp_file = tmp_path / "MetricsChannelMetadata.tachyon"
        protocol.write_tachyon_to_file(config, tmp_file)
        config_from_file = protocol.read_tachyon_from_file(
            metrics_channel_metadata_config.MetricsChannelMetadataConfig, tmp_file
        )
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
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)
    config_by_domain = gen_metrics_channel_metadata_configs.gen_metrics_channel_metadata_configs(physical_system)
    assert len(config_by_domain.values()) == 2

    for config in config_by_domain.values():
        test_serialization(config)

    cpu1_uuid = None
    cpu2_uuid = None

    for domain_uuid, domain in physical_system.cpu_domains.items():
        if domain.logical.name == "Cpu1":
            cpu1_uuid = domain_uuid
        else:
            assert domain.logical.name == "Cpu2"
            cpu2_uuid = domain_uuid

    assert cpu1_uuid
    assert cpu2_uuid

    for channel in config_by_domain[cpu1_uuid].metrics_channels:
        assert channel.cog_instance_path in {cog.fqn for cog in logical_system.cogs.values()}
        assert channel.cog_path in {cog.cog_class.fqn for cog in logical_system.cogs.values()}
        assert channel.metrics_channel_name in {
            channel.channel.channel_name for channel in logical_system.metrics_channels.values()
        }
    assert (
        config_by_domain[cpu1_uuid].metrics_metadata_report_schema_name
        == f"@clockwork::clockwork::tools::metrics_channel_metadata::metrics_channel_metadata_config::MetricsChannelMetadataReport<max_channel_name_size={constants.MAX_CHANNEL_NAME_SIZE},max_num_channels=2046>"
    )

    for channel in config_by_domain[cpu2_uuid].metrics_channels:
        assert channel.cog_instance_path in {cog.fqn for cog in logical_system.cogs.values()}
        assert channel.cog_path in {cog.cog_class.fqn for cog in logical_system.cogs.values()}
        assert channel.metrics_channel_name in {
            channel.channel.channel_name for channel in logical_system.metrics_channels.values()
        }
    assert (
        config_by_domain[cpu2_uuid].metrics_metadata_report_schema_name
        == f"@clockwork::clockwork::tools::metrics_channel_metadata::metrics_channel_metadata_config::MetricsChannelMetadataReport<max_channel_name_size={constants.MAX_CHANNEL_NAME_SIZE},max_num_channels=2046>"
    )
