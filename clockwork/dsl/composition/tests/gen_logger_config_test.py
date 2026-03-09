# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for logger configs generation."""

from pathlib import Path

import pytest
from clockwork.dsl.composition import gen_logger_configs, logger_config, logger_config_proto, system
from clockwork.dsl.ir import box, compiler, importer_registry, pubsub
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.py import protocol


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_gen_configs(tmp_path: Path, fs_importer: FilesystemImporter) -> None:

    # Generate configs
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )

    # Store the importer in the module's context for logger_config to use
    importer_reg = module.context[importer_registry.IMPORTER_REGISTRY_KEY]
    importer_reg.importer = fs_importer

    entities = logger_config.get_entities(module.context)

    def basic_helper(config: logger_config_proto.LogWriterConfig) -> None:
        # Test serialization/deserialization
        buffer = bytearray(entities.log_writer_config.get_tachyon_constraint().size)
        config.serialize_tachyon(memoryview(buffer))
        config_from_serdes = entities.log_writer_config.deserialize_tachyon(memoryview(buffer))
        assert config == config_from_serdes
        # Test read/write file
        tmp_file = tmp_path / "LogWriterConfig.tachyon"
        protocol.write_tachyon_to_file(config, tmp_file)
        config_from_file = protocol.read_tachyon_from_file(entities.log_writer_config, tmp_file)
        assert config == config_from_file

    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)
    configs = gen_logger_configs.gen_logger_configs(physical_system)

    for domain_uuid, domain in physical_system.cpu_domains.items():
        if domain.logical.name == "Cpu1":
            cpu1_uuid = domain_uuid
        else:
            assert domain.logical.name == "Cpu2"
            cpu2_uuid = domain_uuid

    basic_helper(configs[cpu1_uuid].events_config)  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    event_conf1 = configs[cpu1_uuid].events_config  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(event_conf1.channels) == 3
    assert {ch.channel_name for ch in event_conf1.channels} == {"Chan1", "MultiChan1"}
    basic_helper(configs[cpu1_uuid].telemetry_config)  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    tel_conf1 = configs[cpu1_uuid].telemetry_config  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(tel_conf1.channels) == 1
    assert {ch.channel_name for ch in tel_conf1.channels} == {"Chan1"}
    chan1_conf = tel_conf1.channels[0]
    assert chan1_conf.schema_encoding == entities.schema_encoding.clockwork_tachyon
    chan1 = module.inner_scope.lookup("Chan1")
    assert isinstance(chan1, pubsub.Channel)
    assert chan1.message_repr is not None
    hello_msg = chan1.message_repr.get_schema()
    metadata = tachyon_metadata.get_serialized_metadata(module.context, hello_msg)
    assert chan1_conf.schema_definition == list(metadata)
    assert len(metadata) > 0

    basic_helper(configs[cpu2_uuid].events_config)  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    event_conf2 = configs[cpu2_uuid].events_config  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(event_conf2.channels) == 4
    assert {ch.channel_name for ch in event_conf2.channels} == {"Chan1", "Chan2", "MultiChan2"}
    basic_helper(configs[cpu2_uuid].telemetry_config)  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    tel_conf2 = configs[cpu2_uuid].telemetry_config  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(tel_conf2.channels) == 2
    assert {ch.channel_name for ch in tel_conf2.channels} == {"Chan1", "Chan2"}


def test_gen_logged_channel_metadata(fs_importer: FilesystemImporter) -> None:
    # Generate logged channel metadata
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )

    # Store the importer in the module's context for logger_config to use
    importer_reg = module.context[importer_registry.IMPORTER_REGISTRY_KEY]
    importer_reg.importer = fs_importer

    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)
    metadata = gen_logger_configs.gen_logged_channel_metadata(physical_system)

    assert set(metadata.channel_metadata) == {
        "Chan1",
        "MultiChan1",
        "MultiChan2",
    }

    for channel_name in metadata.channel_metadata:
        channel = module.inner_scope.lookup(channel_name)
        assert isinstance(channel, pubsub.Channel)
        assert channel.message_repr is not None
        channel_msg = channel.message_repr.get_schema()
        channel_metadata = tachyon_metadata.get_serialized_metadata(module.context, channel_msg)
        assert channel_metadata == metadata.channel_metadata[channel_name].SerializeToString()
