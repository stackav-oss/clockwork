# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for configurations for subscribers to multi-publisher channels."""

from dataclasses import asdict
from pathlib import Path

import pytest
from clockwork.dsl.composition import (
    gen_multi_subscriber_configs,
    multi_subscriber_config,
    multi_subscriber_config_proto,
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
    def basic_helper(config: multi_subscriber_config_proto.MultiSubscriberConfig) -> None:
        # Test serialization/deserialization
        buffer = bytearray(multi_subscriber_config.MultiSubscriberConfig.get_tachyon_constraint().size)
        config.serialize_tachyon(memoryview(buffer))
        config_from_serdes = multi_subscriber_config.MultiSubscriberConfig.deserialize_tachyon(memoryview(buffer))
        assert asdict(config) == asdict(config_from_serdes)
        # Test read/write file
        tmp_file = tmp_path / "MultiSubscriberConfig.tachyon"
        protocol.write_tachyon_to_file(config, tmp_file)
        config_from_file = protocol.read_tachyon_from_file(multi_subscriber_config.MultiSubscriberConfig, tmp_file)
        assert asdict(config) == asdict(config_from_file)

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
    configs = gen_multi_subscriber_configs.gen_multi_subscriber_configs(physical_system)

    assert len(configs) == 2

    for domain_uuid, domain in physical_system.cpu_domains.items():
        if domain.logical.name == "Cpu1":
            cpu1_uuid = domain_uuid
        else:
            assert domain.logical.name == "Cpu2"
            cpu2_uuid = domain_uuid

    assert cpu1_uuid in configs  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(configs[cpu1_uuid]) == 1  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert "clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan2" in configs[cpu1_uuid]  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    multichan2 = configs[cpu1_uuid]["clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan2"]  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(multichan2.publisher_ids) == 2
    basic_helper(multichan2)

    assert cpu2_uuid in configs  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(configs[cpu2_uuid]) == 1  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert "clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan1" in configs[cpu2_uuid]  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    multichan1 = configs[cpu2_uuid]["clockwork.clockwork.dsl.composition.tests.support.simplesys.MultiChan1"]  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(multichan1.publisher_ids) == 2
    basic_helper(multichan1)
