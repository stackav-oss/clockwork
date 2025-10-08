# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for tachyon_dyn_from_metadata."""

from __future__ import annotations

import re
import uuid
from decimal import Decimal
from pathlib import Path
from typing import Any

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, primitive, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.metadata import tachyon as tachyon_metadata
from clockwork.serialization.py import tachyon_dyn, tachyon_dyn_from_metadata


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_tapmsg(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")), fs_importer
    )
    tap_msg = module.inner_scope.lookup("TapMsg")
    assert isinstance(tap_msg, schema.Schema)
    tap_msg_ir = schema.InstantiatedSchema.make(
        tap_msg.get_resolved(),
        {"signed_value": primitive.DecimalValue(value=Decimal(234), type_info=clkbuiltins.INT64)},
    )

    TapMsgMeta: Any  # noqa: N806 Represents a type and should be camel case
    TapMsgMeta, py_types = tachyon_dyn_from_metadata.py_type_from_metadata(  # noqa: N806  Represents a type and should be camel case
        module.context,
        tap_msg_ir.value_key(),
        tachyon_metadata.get_metadata(module.context, tap_msg_ir),
    )
    SomeEnumMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum"]  # noqa: N806 Represents a type and should be camel case
    SomeFlagsMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags"]  # noqa: N806 Represents a type and should be camel case
    SubMsgMeta = py_types[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SubMsg"]  # noqa: N806 Represents a type and should be camel case
    TapMsgDyn, _ = tachyon_dyn.get_instantiation_dataclass(module.context, module, "TapMsg", signed_value=234)  # noqa: N806 Represents a type and should be camel case
    SomeEnumDyn, _ = tachyon_dyn.get_enum(module.context, module, "SomeEnum")  # noqa: N806 Represents a type and should be camel case
    SomeFlagsDyn, _ = tachyon_dyn.get_enum(module.context, module, "SomeFlags")  # noqa: N806 Represents a type and should be camel case
    SubMsgDyn, _ = tachyon_dyn.get_schema_dataclass(module.context, module, "SubMsg")  # noqa: N806 Represents a type and should be camel case
    uuid1 = uuid.uuid4()
    uuid2 = uuid.uuid4()
    msg_meta = TapMsgMeta(
        integer=1,
        floating_point=2.0,
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["one", "two"],
        uuid=uuid1,
        uuid_different_namespace=uuid2,
        default_enum=SomeEnumMeta.second_value,
        enum_with_init=SomeEnumMeta.first_value,
        default_flags=SomeFlagsMeta.flag12,
        flags_with_init=SomeFlagsMeta.flag3 | SomeFlagsMeta.flag12,
        nested_schema=SubMsgMeta(field=42),
        array_of_schema=[SubMsgMeta(field=11)],
        duration=1234,
        sync_time=4567,
        optional=None,
        bool_with_init=False,
        strong_type=3,
        external_strong_type=4,
        fixed_array=list(range(2)),
        var_string="a",
        integer_with_init=456,
    )
    msg_dyn = TapMsgDyn(
        integer=1,
        floating_point=2.0,
        boolean=True,
        array_of_primitives=list(range(9)),
        array_of_array=["one", "two"],
        uuid=uuid1,
        uuid_different_namespace=uuid2,
        default_enum=SomeEnumDyn.second_value,
        enum_with_init=SomeEnumDyn.first_value,
        default_flags=SomeFlagsDyn.flag12,
        flags_with_init=SomeFlagsDyn.flag3 | SomeFlagsDyn.flag12,
        nested_schema=SubMsgDyn(field=42),
        array_of_schema=[SubMsgDyn(field=11)],
        duration=1234,
        sync_time=4567,
        optional=None,
        bool_with_init=False,
        strong_type=3,
        external_strong_type=4,
        fixed_array=list(range(2)),
        var_string="a",
        integer_with_init=456,
    )
    assert TapMsgDyn.get_tachyon_constraint() == TapMsgMeta.get_tachyon_constraint()
    assert TapMsgDyn.get_tachyon_metadata() == TapMsgMeta.get_tachyon_metadata()
    assert TapMsgDyn.get_tachyon_metadata_name() == TapMsgMeta.get_tachyon_metadata_name()
    buffer = bytearray(TapMsgDyn.get_tachyon_constraint().size)
    msg_meta.serialize_tachyon(memoryview(buffer))
    msg_meta2 = TapMsgMeta.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_meta2 == msg_meta
    msg_dyn2 = TapMsgDyn.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_dyn2 == msg_dyn
    msg_meta.optional = 13
    msg_meta.array_of_array = []
    msg_meta.array_of_schema.append(SubMsgMeta(field=3))
    msg_meta2.serialize_tachyon(memoryview(buffer))
    msg_meta3 = TapMsgMeta.deserialize_tachyon(memoryview(bytes(buffer)))
    assert msg_meta3 == msg_meta2

    msg_meta.array_of_schema.append(SubMsgDyn(field=3))
    with pytest.raises(ValueError, match=re.escape("Attempt to serialize array of length 3, max 2")):
        msg_meta.serialize_tachyon(memoryview(buffer))
