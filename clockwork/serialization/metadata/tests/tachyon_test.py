# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

from decimal import Decimal
from pathlib import Path
from uuid import UUID

import pytest
from clockwork.dsl.ir import clkbuiltins, compiler, primitive, schema
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.metadata import tachyon
from clockwork.serialization.metadata import tachyon_model as model
from clockwork.serialization.metadata import tachyon_model_pb2 as model_pb2
from google.protobuf import json_format


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_metadata(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/tapmsg.clk")), fs_importer
    )
    tap_msg = module.inner_scope.lookup("TapMsg")
    assert isinstance(tap_msg, schema.Schema)
    schema_ir = schema.InstantiatedSchema.make(
        tap_msg.get_resolved(),
        {"signed_value": primitive.DecimalValue(value=Decimal(234), type_info=clkbuiltins.INT64)},
    )
    builder = tachyon.Builder(module.context)
    schema_id = builder.handle_type(schema_ir)
    assert len(builder.types) == 25
    assert sorted(builder.value_key_to_id) == [
        "::Bool",
        "::Duration",
        "::FixedArray<type=::Int32,size=2>",
        "::Float32",
        "::Int32",
        "::Int64",
        "::Optional<type=::UInt32>",
        "::SyncTime",
        "::UInt32",
        "::UInt64",
        "::UInt8",
        "::Uuid<tag=@clockwork::clockwork::dsl::tests::support::tapmsg::SubMsg>",
        "::Uuid<tag=@clockwork::clockwork::dsl::tests::support::taptags::AnotherTag>",
        "::VarArray<type=::Int32,max_size=9>",
        "::VarArray<type=::VarString<max_size=3>,max_size=2>",
        "::VarArray<type=@clockwork::clockwork::dsl::tests::support::tapmsg::SubMsg,max_size=2>",
        "::VarString<max_size=2>",
        "::VarString<max_size=3>",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::ExternalStrongType",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::MyStrongType",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SubMsg",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::TapMsg<signed_value=234>",
        f"@{CLK_REPO}::clockwork::dsl::tests::support::taptags::AnotherTag",
    ]
    assert builder.value_key_to_id[schema_ir.value_key()] == schema_id
    schema_type = builder.types[schema_id]
    assert isinstance(schema_type, model.SchemaType)
    assert schema_type == model.SchemaType(
        fqn=f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::TapMsg",
        size=240,
        alignment=8,
        schema_uuid=UUID("cae6ee0b-ec41-40cf-8b87-fb1583e5985d"),
        version=24,
        arguments=("234",),
        fields=(
            model.SchemaField(offset=160, num=1, name="integer", type_id=builder.value_key_to_id["::Int64"]),
            model.SchemaField(
                offset=216,
                num=2,
                name="floating_point",
                type_id=builder.value_key_to_id["::Float32"],
                init_value=model.FloatInitialValue(value=Decimal("1.234")),
            ),
            model.SchemaField(offset=228, num=3, name="boolean", type_id=builder.value_key_to_id["::Bool"]),
            model.SchemaField(
                offset=0,
                num=4,
                name="array_of_primitives",
                type_id=builder.value_key_to_id["::VarArray<type=::Int32,max_size=9>"],
            ),
            model.SchemaField(
                offset=48,
                num=5,
                name="array_of_array",
                type_id=builder.value_key_to_id["::VarArray<type=::VarString<max_size=3>,max_size=2>"],
            ),
            model.SchemaField(
                offset=112,
                num=6,
                name="uuid",
                type_id=builder.value_key_to_id[
                    "::Uuid<tag=@clockwork::clockwork::dsl::tests::support::tapmsg::SubMsg>"
                ],
            ),
            model.SchemaField(
                offset=128,
                num=7,
                name="uuid_different_namespace",
                type_id=builder.value_key_to_id[
                    "::Uuid<tag=@clockwork::clockwork::dsl::tests::support::taptags::AnotherTag>"
                ],
            ),
            model.SchemaField(
                offset=229,
                num=8,
                name="default_enum",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum"],
            ),
            model.SchemaField(
                offset=230,
                num=9,
                name="enum_with_init",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum"],
                init_value=model.UnsignedInitialValue(value=1),
            ),
            model.SchemaField(
                offset=168,
                num=10,
                name="nested_schema",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SubMsg"],
            ),
            model.SchemaField(
                offset=88,
                num=11,
                name="array_of_schema",
                type_id=builder.value_key_to_id[
                    "::VarArray<type=@clockwork::clockwork::dsl::tests::support::tapmsg::SubMsg,max_size=2>"
                ],
            ),
            model.SchemaField(offset=176, num=12, name="duration", type_id=builder.value_key_to_id["::Duration"]),
            model.SchemaField(offset=184, num=13, name="sync_time", type_id=builder.value_key_to_id["::SyncTime"]),
            model.SchemaField(
                offset=200, num=14, name="optional", type_id=builder.value_key_to_id["::Optional<type=::UInt32>"]
            ),
            model.SchemaField(
                offset=231,
                num=15,
                name="bool_with_init",
                type_id=builder.value_key_to_id["::Bool"],
                init_value=model.BoolInitialValue(value=True),
            ),
            model.SchemaField(
                offset=192,
                num=16,
                name="strong_type",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::MyStrongType"],
            ),
            model.SchemaField(
                offset=220,
                num=17,
                name="external_strong_type",
                type_id=builder.value_key_to_id[
                    f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::ExternalStrongType"
                ],
                init_value=model.UnsignedInitialValue(value=123),
            ),
            model.SchemaField(
                offset=208,
                num=18,
                name="fixed_array",
                type_id=builder.value_key_to_id["::FixedArray<type=::Int32,size=2>"],
            ),
            model.SchemaField(
                offset=232,
                num=20,
                name="default_flags",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags"],
            ),
            model.SchemaField(
                offset=233,
                num=21,
                name="flags_with_init",
                type_id=builder.value_key_to_id[f"@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags"],
                init_value=model.UnsignedInitialValue(value=2),
            ),
            model.SchemaField(
                offset=144, num=23, name="var_string", type_id=builder.value_key_to_id["::VarString<max_size=2>"]
            ),
            model.SchemaField(
                offset=224,
                num=24,
                name="integer_with_init",
                type_id=builder.value_key_to_id["::Int32"],
                init_value=model.SignedInitialValue(value=234),
            ),
        ),
        hash=b"\xf1\x90\n\xb9\x0e\xd1\x19\xb3<+\rln\x95\x16\x81",
    )
    fix_array_int_2 = builder.types[builder.value_key_to_id["::FixedArray<type=::Int32,size=2>"]]
    assert isinstance(fix_array_int_2, model.BuiltInType)
    assert fix_array_int_2 == model.BuiltInType(
        fqn=".FixedArray",
        uuid=UUID("9c9f69dc-e0be-5333-8f2a-8d8c20dca531"),
        size=8,
        alignment=4,
        arguments=(builder.value_key_to_id["::Int32"], "2"),
        hash=b"\xe1\xf8Q\x1f\xe6l\x18\t\x9f\xd7Y\x1c;g\x9c%",
    )
    # Now make a minor change and check that the hash changes
    fix_array_int_2.arguments = (fix_array_int_2.arguments[0], "3")
    assert (
        schema_type.get_hash(builder.types, force_recompute=True) == b"\x8d\xecm\x0f\n\xbar\xa0\x1e\x1f\\\x8f\x12o@\xaf"
    )
    meta = builder.get_metadata(schema_id)
    pb_meta = tachyon.to_protobuf(meta)
    assert pb_meta is not None
    assert len(pb_meta.types) == len(meta.types)
    # This output can be helpful when the underlying message changes, requiring
    # changes to the JSON-serialized metadata below.
    print(json_format.MessageToJson(pb_meta))
    meta2 = tachyon.from_protobuf(pb_meta)
    assert meta2 == meta
    pb_bytes = pb_meta.SerializeToString()
    pb_meta2 = pb_meta.FromString(pb_bytes)
    assert pb_meta2 == pb_meta
    meta3 = tachyon.from_protobuf(pb_meta2)
    assert meta3 == meta
    expected_json = f"""{{
  "outer_type_id": 0,
  "types": [
    {{
      "schema": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::TapMsg",
        "size": 240,
        "alignment": 8,
        "schemaUuid": "yubuC+xBQM+Lh/sVg+WYXQ==",
        "version": 24,
        "fields": [
          {{
            "offset": 160,
            "num": 1,
            "name": "integer",
            "typeId": 1
          }},
          {{
            "offset": 216,
            "num": 2,
            "name": "floating_point",
            "typeId": 2,
            "initValue": {{
              "floatValue": "1.234"
            }}
          }},
          {{
            "offset": 228,
            "num": 3,
            "name": "boolean",
            "typeId": 3
          }},
          {{
            "num": 4,
            "name": "array_of_primitives",
            "typeId": 4
          }},
          {{
            "offset": 48,
            "num": 5,
            "name": "array_of_array",
            "typeId": 6
          }},
          {{
            "offset": 112,
            "num": 6,
            "name": "uuid",
            "typeId": 8
          }},
          {{
            "offset": 128,
            "num": 7,
            "name": "uuid_different_namespace",
            "typeId": 10
          }},
          {{
            "offset": 229,
            "num": 8,
            "name": "default_enum",
            "typeId": 13
          }},
          {{
            "offset": 230,
            "num": 9,
            "name": "enum_with_init",
            "typeId": 13,
            "initValue": {{
              "unsignedValue": "1"
            }}
          }},
          {{
            "offset": 168,
            "num": 10,
            "name": "nested_schema",
            "typeId": 9
          }},
          {{
            "offset": 88,
            "num": 11,
            "name": "array_of_schema",
            "typeId": 14
          }},
          {{
            "offset": 176,
            "num": 12,
            "name": "duration",
            "typeId": 15
          }},
          {{
            "offset": 184,
            "num": 13,
            "name": "sync_time",
            "typeId": 16
          }},
          {{
            "offset": 200,
            "num": 14,
            "name": "optional",
            "typeId": 17
          }},
          {{
            "offset": 231,
            "num": 15,
            "name": "bool_with_init",
            "typeId": 3,
            "initValue": {{
              "boolValue": true
            }}
          }},
          {{
            "offset": 192,
            "num": 16,
            "name": "strong_type",
            "typeId": 20
          }},
          {{
            "offset": 220,
            "num": 17,
            "name": "external_strong_type",
            "typeId": 21,
            "initValue": {{
              "unsignedValue": "123"
            }}
          }},
          {{
            "offset": 208,
            "num": 18,
            "name": "fixed_array",
            "typeId": 22
          }},
          {{
            "offset": 232,
            "num": 20,
            "name": "default_flags",
            "typeId": 23
          }},
          {{
            "offset": 233,
            "num": 21,
            "name": "flags_with_init",
            "typeId": 23,
            "initValue": {{
              "unsignedValue": "2"
            }}
          }},
          {{
            "offset": 144,
            "num": 23,
            "name": "var_string",
            "typeId": 24
          }},
          {{
            "offset": 224,
            "num": 24,
            "name": "integer_with_init",
            "typeId": 5,
            "initValue": {{
              "signedValue": "234"
            }}
          }}
        ],
        "hash": "jextDwq6cqAeH1yPEm9Arw==",
        "history": {{
          "removed": [
            22
          ],
          "became": {{
            "19": 23
          }}
        }},
        "arguments": [
          {{
            "value": "234"
          }}
        ]
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Int64",
        "size": 8,
        "alignment": 8,
        "hash": "9TQTIHp78INrjfjJlWxViw==",
        "uuid": "p47jupElXDu3SfsWgjTjIA=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Float32",
        "size": 4,
        "alignment": 4,
        "hash": "mH4dlJ2QjCEf4Dii/hv7mw==",
        "uuid": "ZoNAVhK/WdC+jasV2WRSDQ=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Bool",
        "size": 1,
        "alignment": 1,
        "hash": "Ux2BqzfLRVwESsiktWMinw==",
        "uuid": "FGRYvqlbWl+5G/HBMKKJrA=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".VarArray",
        "size": 48,
        "alignment": 8,
        "arguments": [
          {{
            "typeId": 5
          }},
          {{
            "value": "9"
          }}
        ],
        "hash": "JuKb2eMsJgO/VTmW+Y9i9w==",
        "uuid": "lUsqAqnVX0iIJfUM9lQ51g=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Int32",
        "size": 4,
        "alignment": 4,
        "hash": "C5SP40cL6Tg4Zre3ozO42w==",
        "uuid": "5RfhI2ItU7WOynk/QH5eKA=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".VarArray",
        "size": 40,
        "alignment": 8,
        "arguments": [
          {{
            "typeId": 7
          }},
          {{
            "value": "2"
          }}
        ],
        "hash": "ijYZEmf2R9BOARrtB04J5A==",
        "uuid": "lUsqAqnVX0iIJfUM9lQ51g=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".VarString",
        "size": 16,
        "alignment": 8,
        "arguments": [
          {{
            "value": "3"
          }}
        ],
        "hash": "KNrnnlj0+MzLE/hbspHxMw==",
        "uuid": "7OKUZWNQVjGbYX8uTKOb4w=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Uuid",
        "size": 16,
        "alignment": 8,
        "arguments": [
          {{
            "typeId": 9
          }}
        ],
        "hash": "FjdUhoUz9at2Ki0p+EkJng==",
        "uuid": "kRRCCnZqX1qQ5EBNOq59Pw=="
      }}
    }},
    {{
      "schema": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SubMsg",
        "size": 8,
        "alignment": 8,
        "schemaUuid": "M13U1HdTSqalqfOfYFVqrg==",
        "version": 1,
        "fields": [
          {{
            "num": 1,
            "name": "field",
            "typeId": 1,
            "initValue": {{
              "signedValue": "99"
            }}
          }}
        ],
        "hash": "M20viTs+0+VatnGxcdgzlg=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Uuid",
        "size": 16,
        "alignment": 8,
        "arguments": [
          {{
            "typeId": 11
          }}
        ],
        "hash": "FjdUhoUz9at2Ki0p+EkJng==",
        "uuid": "kRRCCnZqX1qQ5EBNOq59Pw=="
      }}
    }},
    {{
      "tag": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::taptags::AnotherTag",
        "hash": "GPCljJfOjjiEcBw1fICoDQ=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".UInt8",
        "size": 1,
        "alignment": 1,
        "hash": "yMmVnKhgqfpVlQmRTnMhbQ==",
        "uuid": "LjBFvt7zWHC5d6iARd9JQA=="
      }}
    }},
    {{
      "clkEnum": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeEnum",
        "underlyingTypeId": 12,
        "enumUuid": "AtGmAVxpRt2ZIgb+4ztYHg==",
        "version": 4,
        "values": [
          {{
            "num": 3,
            "name": "first_value"
          }},
          {{
            "num": 4,
            "value": "1",
            "name": "second_value"
          }}
        ],
        "hash": "Z2N4r1BR4dT55hmAd6S8Vg==",
        "history": {{
          "removed": [
            2
          ],
          "became": {{
            "1": 3
          }}
        }}
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".VarArray",
        "size": 24,
        "alignment": 8,
        "arguments": [
          {{
            "typeId": 9
          }},
          {{
            "value": "2"
          }}
        ],
        "hash": "EymCHCtFV1yrNAvZrvjmBw==",
        "uuid": "lUsqAqnVX0iIJfUM9lQ51g=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Duration",
        "size": 8,
        "alignment": 8,
        "hash": "OfxsEEmRudYn6u5V67pSgQ==",
        "uuid": "0HOW3xRuU0uDbzI7NjAGaA=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".SyncTime",
        "size": 8,
        "alignment": 8,
        "hash": "T0cEEx+yA3l5LHvtwDIFVQ==",
        "uuid": "i7glKO8zUUC2gQm57naj4w=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".Optional",
        "size": 8,
        "alignment": 4,
        "arguments": [
          {{
            "typeId": 18
          }}
        ],
        "hash": "dYXa7PTkHYmVMzJO7fFE9g==",
        "uuid": "OIMwjjjhXL+ozRaqicFDXw=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".UInt32",
        "size": 4,
        "alignment": 4,
        "hash": "dpink3VAYXqYKQPeFNvhVg==",
        "uuid": "MtyCEFxaXcK+W+iXlBmv5Q=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".UInt64",
        "size": 8,
        "alignment": 8,
        "hash": "qJ08RwHQ6ylL5XXgeLQRmQ==",
        "uuid": "+doagbq2Vy+/YS86jlhlYA=="
      }}
    }},
    {{
      "strongType": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::MyStrongType",
        "underlyingTypeId": 19,
        "hash": "qJ08RwHQ6ylL5XXgeLQRmQ=="
      }}
    }},
    {{
      "strongType": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::ExternalStrongType",
        "underlyingTypeId": 18,
        "hash": "dpink3VAYXqYKQPeFNvhVg=="
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".FixedArray",
        "size": 8,
        "alignment": 4,
        "arguments": [
          {{
            "typeId": 5
          }},
          {{
            "value": "3"
          }}
        ],
        "hash": "9gpkUrV0a0lMisfPGJW1IA==",
        "uuid": "nJ9p3OC+UzOPKo2MINylMQ=="
      }}
    }},
    {{
      "clkEnum": {{
        "fqn": "@{CLK_REPO}::clockwork::dsl::tests::support::tapmsg::SomeFlags",
        "underlyingTypeId": 12,
        "enumUuid": "nWbu+BAZSKKXHvm7iy3wbA==",
        "version": 6,
        "values": [
          {{
            "name": "none"
          }},
          {{
            "num": 3,
            "value": "1",
            "name": "flag1"
          }},
          {{
            "num": 4,
            "value": "2",
            "name": "flag2"
          }},
          {{
            "num": 5,
            "value": "4",
            "name": "flag3"
          }},
          {{
            "num": 6,
            "value": "3",
            "name": "flag12"
          }}
        ],
        "hash": "dPIfhUZnAHdD2BzGfXm4sQ==",
        "options": 1,
        "history": {{
          "removed": [
            2
          ],
          "became": {{
            "1": 3
          }}
        }}
      }}
    }},
    {{
      "builtIn": {{
        "fqn": ".VarString",
        "size": 16,
        "alignment": 8,
        "arguments": [
          {{
            "value": "2"
          }}
        ],
        "hash": "xw/ca2d+YOYCyxGmJLrJLg==",
        "uuid": "7OKUZWNQVjGbYX8uTKOb4w=="
      }}
    }}
  ],
  "version": 3
}}"""
    expected = json_format.Parse(expected_json, model_pb2.TachyonMetadata())
    assert json_format.MessageToJson(expected) == json_format.MessageToJson(pb_meta2)
    assert expected == pb_meta2
