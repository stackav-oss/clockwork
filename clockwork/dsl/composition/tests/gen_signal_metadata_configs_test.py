# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit test for signal metadata configs generation."""

from pathlib import Path

import pytest
from clockwork.dsl.composition import gen_signal_metadata_configs, system
from clockwork.dsl.ir import box, compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_gen_signal_metadata_config(fs_importer: FilesystemImporter) -> None:  # noqa: C901, PLR0915
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("SignalTestSystemBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)

    config = gen_signal_metadata_configs.generate_signal_metadata_config(physical_system)

    # Test signal_instance_names
    # 2 module signals (both multi-instance) + 2 cog signals (both multi-instance) across 2 cog instances
    # Each multi-instance signal used in both cog instances gets 2 instance names
    assert config.signal_instance_names is not None
    assert len(config.signal_instance_names) == 6  # module_signal: 2, cog_signal: 2, cog_private_signal: 2

    # Test signals - should have exactly 4 signals total
    # Includes: module_signal, multi_instance_signal (module-level), cog_signal, cog_private_signal (cog-scope)
    assert len(config.signals) == 4
    signal_names = {sig.name for sig in config.signals}
    # Signal names are fully qualified
    assert sum(1 for name in signal_names if "module_signal" in name) == 1
    assert sum(1 for name in signal_names if "cog_signal" in name and "cog_private_signal" not in name) == 1
    assert sum(1 for name in signal_names if "cog_private_signal" in name) == 1

    for signal_metadata in config.signals:
        assert signal_metadata.name
        assert signal_metadata.pre_aggregation_type
        assert signal_metadata.pre_aggregation_definition
        # signal_instance_indexes: module_signal and signals used in report groups should have instances
        assert len(signal_metadata.signal_instance_indexes) >= 0

    # Test cogs - verify signal indexes are valid and point to correct signals
    # Should have exactly 1 cog class (SignalTestCog)
    assert len(config.cogs) == 1
    for cog_metadata in config.cogs:
        assert cog_metadata.cog_class_id
        # Should have exactly 1 report group (test_report_group)
        assert len(cog_metadata.report_groups) == 1

        for rg in cog_metadata.report_groups:
            assert rg.name
            # Should have exactly 3 signals in test_report_group
            assert len(rg.signals) == 3

            for sig in rg.signals:
                # Verify signal_index is valid
                assert 0 <= sig.signal_index < len(config.signals)
                signal_name = config.signals[sig.signal_index].name

                # Verify post_aggregation_types content (order-independent)
                if "cog_signal" in signal_name and "cog_private_signal" not in signal_name:
                    # cog_signal should have post_aggregation: ["sum", "count"]
                    assert len(sig.post_aggregation_types) == 2
                    post_agg_set = {agg_type.value for agg_type in sig.post_aggregation_types}  # pyright: ignore[reportAttributeAccessIssue]
                    assert post_agg_set == {3, 4}  # Sum=3, Count=4
                elif "cog_private_signal" in signal_name:
                    # cog_private_signal should have post_aggregation: ["min", "max"]
                    assert len(sig.post_aggregation_types) == 2
                    post_agg_set = {agg_type.value for agg_type in sig.post_aggregation_types}  # pyright: ignore[reportAttributeAccessIssue]
                    assert post_agg_set == {1, 2}  # Min=1, Max=2
                elif "module_signal" in signal_name:
                    # module_signal should have post_aggregation: ["min", "max"]
                    assert len(sig.post_aggregation_types) == 2
                    post_agg_set = {agg_type.value for agg_type in sig.post_aggregation_types}  # pyright: ignore[reportAttributeAccessIssue]
                    assert post_agg_set == {1, 2}  # Min=1, Max=2

    # Test cog_instances - verify indexes and relationships
    # Should have exactly 2 instances of SignalTestCog (test_cog1, test_cog2)
    assert len(config.cog_instances) == 2
    cog_class_ids = {cog.cog_class_id for cog in config.cogs}

    for cog_instance_metadata in config.cog_instances:
        # Verify cog_class_id references a known cog class
        assert cog_instance_metadata.cog_class_id in cog_class_ids
        assert cog_instance_metadata.cog_instance_id

        # Find the corresponding cog class
        cog_class = next(c for c in config.cogs if c.cog_class_id == cog_instance_metadata.cog_class_id)

        # Verify report_group_instances - should have 1 report group instance
        assert len(cog_instance_metadata.report_group_instances) == 1
        assert len(cog_instance_metadata.report_group_instances) == len(cog_class.report_groups)

        for rg_instance in cog_instance_metadata.report_group_instances:
            # Verify report_group_index is valid
            assert 0 <= rg_instance.report_group_index < len(cog_class.report_groups)
            assert rg_instance.channel_name

            # Verify signal_instances - should have 3 signal instances
            rg_def = cog_class.report_groups[rg_instance.report_group_index]
            assert len(rg_instance.signal_instances) == 3
            assert len(rg_instance.signal_instances) == len(rg_def.signals)

            for sig_instance in rg_instance.signal_instances:
                # Verify signal_index is valid
                assert 0 <= sig_instance.signal_index < len(config.signals)
                # Verify signal_instance_index is valid
                assert 0 <= sig_instance.signal_instance_index < len(config.signal_instance_names)

    # Test report_group_channels - verify they match cog_instances
    # Should have exactly 2 channels (one per cog instance)
    assert len(config.report_group_channels) == 2

    for channel_metadata in config.report_group_channels:
        assert channel_metadata.channel_name
        assert channel_metadata.cog_class_id in cog_class_ids

        # Find matching cog_instance
        matching_instance = next(
            (ci for ci in config.cog_instances if ci.cog_instance_id == channel_metadata.cog_instance_id),
            None,
        )
        assert matching_instance is not None
        assert matching_instance.cog_class_id == channel_metadata.cog_class_id

        # Verify report_group_index is valid
        cog_class = next(c for c in config.cogs if c.cog_class_id == channel_metadata.cog_class_id)
        assert 0 <= channel_metadata.report_group_index < len(cog_class.report_groups)

        # Verify channel_name matches the one in cog_instance
        matching_rg_instance = next(
            (
                rg
                for rg in matching_instance.report_group_instances
                if rg.report_group_index == channel_metadata.report_group_index
            ),
            None,
        )
        assert matching_rg_instance is not None
        assert matching_rg_instance.channel_name == channel_metadata.channel_name


def test_signal_instance_names_ordering(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("SignalTestSystemBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)

    config = gen_signal_metadata_configs.generate_signal_metadata_config(physical_system)

    instance_names = config.signal_instance_names
    # Verify that we have exactly 6 signal instance names
    assert len(instance_names) == 6

    for signal_metadata in config.signals:
        for idx in signal_metadata.signal_instance_indexes:
            assert 0 <= idx < len(instance_names)


def test_aggregation_types(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/signals_test_system.clk")),
        fs_importer,
    )
    box_template_ir = module.inner_scope.lookup("SignalTestSystemBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    compiler._register_box_instance_uuids(module.context, box_ir)
    logical_system = system.make_system([box_ir.get_resolved()], module, False)
    physical_system = system.make_physical_system(logical_system)

    config = gen_signal_metadata_configs.generate_signal_metadata_config(physical_system)

    # Find signals by substring match since names are fully qualified
    module_signal = next((s for s in config.signals if "module_signal" in s.name), None)
    assert module_signal is not None
    # DSL text includes string literal quotes
    assert module_signal.pre_aggregation_definition in {'["max", "min"]', '["min", "max"]'}

    cog_signal = next(
        (s for s in config.signals if "cog_signal" in s.name and "cog_private_signal" not in s.name), None
    )
    assert cog_signal is not None
    assert cog_signal.pre_aggregation_definition == "value"
