# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for diagnostics."""

from clockwork.dsl.ir import diagnostics


def test_cpp_types() -> None:
    group_id = diagnostics.group_id("test_group")
    assert group_id.render("") == "::clockwork::diagnostics::SignalGroupId::test_group"
    assert set(group_id.includes) == {diagnostics.REPORT_DEFS_HEADER}

    group_type = diagnostics.group_type(group_id)
    assert (
        group_type.render("")
        == "::clockwork::diagnostics::SignalGroup<::clockwork::diagnostics::SignalGroupId::test_group>"
    )
    assert set(group_type.includes) == {diagnostics.REPORT_DEFS_HEADER}

    group_instance_id_type = diagnostics.group_instance_id_type(group_type)
    assert (
        group_instance_id_type.render("")
        == "::clockwork::diagnostics::SignalGroup<::clockwork::diagnostics::SignalGroupId::test_group>::InstanceType"
    )
    assert set(group_instance_id_type.includes) == {diagnostics.REPORT_DEFS_HEADER}

    instance_id = diagnostics.instance_id(group_instance_id_type, "test_instance")
    assert (
        instance_id.render("")
        == "::clockwork::diagnostics::SignalGroup<::clockwork::diagnostics::SignalGroupId::test_group>::InstanceType::test_instance"
    )
    assert set(instance_id.includes) == {diagnostics.REPORT_DEFS_HEADER}

    assert diagnostics.to_instance_id("test_group", "test_instance") == instance_id
