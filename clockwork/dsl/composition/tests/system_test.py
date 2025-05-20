# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for pub_sub."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.composition import system
from clockwork.dsl.ir import box, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hellomod(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloSystem")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(
        cst_node=None, module=module, scope=box_template_ir.scope, name="test", doc=None
    )
    compiler._register_box_instance_uuids(module.context, box_ir)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    sys = system.make_system([box_ir.get_resolved()], module)
    assert sys.module is module
    assert len(sys.channels) == 4
    assert sys.channels.keys() == {
        "HelloChan",
        "Name that doesn't follow reasonable conventions!",
        "many_publishers",
        "NetworkData",
    }
    assert len(sys.cogs) == 12  # 3 cogs per HelloBox, 4 HelloBox instances


def test_simplesys(fs_importer: FilesystemImporter) -> None:  # noqa: PLR0915
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    sys = system.make_system([box_ir.get_resolved()], module)
    assert sys.module is module
    assert len(sys.channels) == 4
    assert sys.channels.keys() == {"Chan1", "Chan2", "MultiChan1", "MultiChan2"}
    assert len(sys.cogs) == 2
    psys = system.make_physical_system(sys)
    for domain in psys.cpu_domains.values():
        if domain.logical.name == "Cpu1":
            cpu1 = domain
        else:
            assert domain.logical.name == "Cpu2"
            cpu2 = domain
    ((obs1_1_uuid, obs1_1), (obs1_2_uuid, obs1_2), (obs1_3_uuid, obs1_3)) = cpu1.bridge_observers.items()  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    ((pro1_1_uuid, pro1_1), (pro1_2_uuid, pro1_2), (pro1_3_uuid, pro1_3)) = cpu1.bridge_producers.items()  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    ((obs2_1_uuid, obs2_1), (obs2_2_uuid, obs2_2), (obs2_3_uuid, obs2_3)) = cpu2.bridge_observers.items()  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    ((pro2_1_uuid, pro2_1), (pro2_2_uuid, pro2_2), (pro2_3_uuid, pro2_3)) = cpu2.bridge_producers.items()  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert obs1_1.dest_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(obs1_1.remote_producers) == 1
    assert obs1_1.remote_producers[0] == pro2_1_uuid
    assert obs1_1.source_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_1.source_pinion_buffer].observers == {obs1_1_uuid: obs1_1}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_1.source_pinion_buffer].uuid == obs1_1.source_pinion_buffer  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_1.source_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_1.dest_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert obs1_2.dest_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(obs1_2.remote_producers) == 1
    assert obs1_2.remote_producers[0] == pro2_2_uuid
    assert obs1_2.source_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_2.source_pinion_buffer].observers == {obs1_2_uuid: obs1_2}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_2.source_pinion_buffer].uuid == obs1_2.source_pinion_buffer  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_2.source_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_2.dest_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert obs1_3.dest_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert len(obs1_3.remote_producers) == 1
    assert obs1_3.remote_producers[0] == pro2_3_uuid
    assert obs1_3.source_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_3.source_pinion_buffer].observers == {obs1_3_uuid: obs1_3}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu1.buffers[obs1_3.source_pinion_buffer].uuid == obs1_3.source_pinion_buffer  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_3.source_domain == cpu2.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert pro1_3.dest_domain == cpu1.uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert cpu1.buffers[pro1_1.dest_pinion_buffer].uuid == pro1_1_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[pro2_1.dest_pinion_buffer].uuid == pro2_1_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[obs2_1.source_pinion_buffer].observers == {obs2_1_uuid: obs2_1}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert cpu1.buffers[pro1_2.dest_pinion_buffer].uuid == pro1_2_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[pro2_2.dest_pinion_buffer].uuid == pro2_2_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[obs2_2.source_pinion_buffer].observers == {obs2_2_uuid: obs2_2}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    assert cpu1.buffers[pro1_3.dest_pinion_buffer].uuid == pro1_3_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[pro2_3.dest_pinion_buffer].uuid == pro2_3_uuid  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    assert cpu2.buffers[obs2_3.source_pinion_buffer].observers == {obs2_3_uuid: obs2_3}  # pyright: ignore[reportPossiblyUnboundVariable] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
