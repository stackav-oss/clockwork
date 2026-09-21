# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for logger configs generation."""

from pathlib import Path

import pytest
from clockwork.dsl.composition import launchgen, system
from clockwork.dsl.ir import box, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_gen_configs(fs_importer: FilesystemImporter) -> None:
    # Generate configs
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/simplesys.clk")),
        fs_importer,
    )

    box_template_ir = module.inner_scope.lookup("System1")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False, True)
    physical_system = system.make_physical_system(logical_system)
    configs = launchgen.gen_simplelaunch_runner_configs(physical_system)
    assert len(configs) == 2

    cpu1_uuid = None
    cpu2_uuid = None
    for domain_uuid, domain in physical_system.cpu_domains.items():
        if domain.logical.name == "Cpu1":
            cpu1_uuid = domain_uuid
        else:
            assert domain.logical.name == "Cpu2"
            cpu2_uuid = domain_uuid
    assert cpu1_uuid is not None
    assert cpu2_uuid is not None

    conf1 = configs[cpu1_uuid]
    assert conf1.host_name == "cpu1"
    assert conf1.diagnostics_config.group_id == "simplelaunch"
    assert conf1.diagnostics_config.instance_id == "cpu1"
    assert conf1.status_publish_endpoint.channel_name == "/cpu1/simplelaunch_status"

    conf2 = configs[cpu2_uuid]
    assert conf2.host_name == "cpu2"
    assert conf2.diagnostics_config.group_id == "simplelaunch"
    assert conf2.diagnostics_config.instance_id == "cpu2"
    assert conf2.status_publish_endpoint.channel_name == "/cpu2/simplelaunch_status"
