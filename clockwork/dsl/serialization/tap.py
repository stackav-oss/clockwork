# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""C++ backend for Clockwork schemas.

This module implements a C++ code generation backend for Clockwork
schemas that creates layout structs to define the data layout.  Then
it implements a wrapper class with methods to get/set each field.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
from decimal import Decimal
from typing import TYPE_CHECKING

from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import literal, typereg, types, values
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, SystemHeader, comment_doc_string
from clockwork.dsl.ir import clkbuiltins, clkenum, node, primitive, schema, strongtypes, typesys
from clockwork.dsl.ir.statement import ImmutableBinding
from clockwork.dsl.ir.uuid_reg import lookup_uuid
from clockwork.dsl.serialization.tachyon_layout import Layout, layout_schema
from clockwork.serialization.metadata import tachyon as tachyon_metadata

if TYPE_CHECKING:
    from collections.abc import Callable, Mapping
    from typing import Final


def _value_to_cpp(value: typesys.Value | None) -> types.CppValueExpr | None:
    """Convert a value to a Cpp type."""
    if value is None:
        return None
    if value in (clkbuiltins.FALSE_VALUE, clkbuiltins.TRUE_VALUE):
        return literal.bool_value_to_cpp(value)
    if isinstance(value, ImmutableBinding):
        assert isinstance(value.value, typesys.Value)  # noqa: S101 (invariant; assured by type system)
        return _value_to_cpp(value.value)
    if isinstance(value, primitive.DecimalValue):
        try:
            return literal.decimal_value_to_cpp(value)
        except NotImplementedError as exc:
            type_info = (
                value.type_info.resolution() if isinstance(value.type_info, typesys.InferenceVar) else value.type_info
            )
            if isinstance(type_info, strongtypes.StrongType):
                msg = f"Unable to convert to literal for StrongType '{type_info.name}'.  Need to register an appropriate factory function in the cpp_target extern block."
                raise RuntimeError(msg) from exc  # noqa: TRY004
            raise
    if isinstance(value, clkenum.ValueRef):
        cpp_enum = typereg.get_cpp_type(value.value_def.enum.module.context, value.value_def.enum)
        return types.CppScopedValue(scope=cpp_enum, header=[], name=value.value_def.name)
    if isinstance(value, schema.ParameterRef):
        return types.CppValue(None, value.name)
    msg = f"Converting {type(value)} to a cpp value is unsupported."
    raise TypeError(msg)


BUILT_IN_PRIMITIVES: Final = {var.value_key() for var in (clkbuiltins.DURATION, clkbuiltins.SYNC_TIME)}


def _treat_as_primitive(field_type: typesys.TypeVal) -> bool:
    """Check if a field type should be treated as a primitive."""
    if isinstance(field_type, clkbuiltins.PrimitiveType | clkenum.ResolvedEnum | strongtypes.StrongType):
        return True
    return field_type.value_key() in BUILT_IN_PRIMITIVES


def _is_fixed_array(field_type: typesys.TypeVal) -> bool:  # pyright: ignore[reportUnusedFunction] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    """Check if a field type should be treated as a primitive."""
    if not isinstance(field_type, typesys.Instantiation):
        return False
    return field_type.instantiates == clkbuiltins.FIXED_ARRAY


def _field_primitive_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert a primitive field to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info
    if not isinstance(field_type, typesys.TypeVal) or not _treat_as_primitive(field_type):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        msg = f"Field type is not supposed to be treated as a pritimive type.  Received: {type(field_type)}"
        raise TypeError(msg)
    cpp_type = typereg.get_cpp_type(compiler_context, field_type)
    init_value = field.init_value
    if isinstance(field_type, clkenum.ClkEnum) and init_value is None:
        init_value = clkenum.ValueRef.make(field_type.values[field_type.default_field_num])
    l_value = types.ref_qualify(cpp_type, types.Ref.L)
    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(init_value),
            doc=f"{field.cur_name}: data member",
        ),
        methods=[
            create_get_method(field.cur_name, types.const_qualify(l_value, True)),
            create_get_mutable_method(field.cur_name, "get_mutable", l_value),
            create_set_method(field.cur_name, cpp_type),
        ],
    )


def require_namespace(entity: node.NamedEntity) -> None:
    """Validate an entity has a `cpp_namespace` attribute."""
    if not hasattr(entity, "cpp_namespace"):
        msg = f"Variable of type {type(entity)} does not have a `cpp_namespace` field."
        raise ValueError(msg)
    if entity.cpp_namespace is None:  # pyright: ignore[reportAttributeAccessIssue] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        msg = f"Variable of type {type(entity)} with name {entity.name} is not instantiated in a cpp_target."
        raise ValueError(msg)


def create_get_method(field_name: str, cpp_type: types.CppTypeExpr, method_prefix: str = "get") -> types.CppMethod:
    """Define a const get method given the field name and type."""
    inline = CppChunk()
    inline.append(f"return fields_.{field_name};")
    return types.CppMethod(
        name=f"{method_prefix}_{field_name}",
        doc=f"{field_name}: A const get method.",
        return_type=cpp_type,
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const", types.Ref.L.value],
        body=inline,
        no_discard=True,
    )


def create_get_mutable_method(field_name: str, method_prefix: str, cpp_type: types.CppTypeExpr) -> types.CppMethod:
    """Define a get_mutable method given the field name and type."""
    inline = CppChunk()
    inline.append(f"return fields_.{field_name};")
    return types.CppMethod(
        name=f"{method_prefix}_{field_name}",
        doc=f"{field_name}: An explicitly mutable get method.",
        return_type=cpp_type,
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[types.Ref.L.value],
        body=inline,
        no_discard=True,
    )


def create_set_method(field_name: str, cpp_type: types.CppTypeExpr) -> types.CppMethod:
    """Define a set method given the field name and type."""
    inline = CppChunk()
    inline.append(f"fields_.{field_name} = new_value;")
    return types.CppMethod(
        name=f"set_{field_name}",
        doc=f"{field_name}: A set method.",
        return_type=types.VOID,
        arguments=[types.CppNamedType(cpp_type, "new_value")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[types.Ref.L.value],
        body=inline,
        no_discard=False,
    )


def _to_cpp_type(
    compiler_context: CompilerContext, type_info: typesys.TypeVal
) -> types.CppType | types.CppTemplateType:
    """Resolve an IR typeto a cpp type."""
    return typereg.get_cpp_type(compiler_context, _to_tap_tachyon(type_info))


def _to_tap_tachyon_helper(
    type_info: typesys.TypeVal,
    schema_transform: Callable[[schema.ResolvedSchema | typesys.Instantiation], typesys.TypeVal],
) -> typesys.TypeVal:
    """Recursively convert a type to a Tachyon or Tap-Tachyon type."""
    if isinstance(type_info, schema.InstantiatedSchema):
        type_info = type_info.as_instantiation_or_resolved_schema()
    if isinstance(type_info, schema.ResolvedSchema):
        return schema_transform(type_info)
    if isinstance(type_info, typesys.Instantiation):
        if type_info.instantiates is clkbuiltins.UUID:
            return type_info
        generic_parameters = type_info.instantiates.generic_parameters()
        if not generic_parameters:
            msg = "An instantiated type is expected to have generic parameters."
            raise ValueError(msg)

        # Shallow copy so we can modify arguments.
        converted = replace(type_info)
        converted.arguments = {}

        for param in generic_parameters:
            if param.type_bound == clkbuiltins.TYPE_TYPE:
                arg = type_info.arguments[param.name]
                assert isinstance(arg, typesys.TypeVal)  # noqa: S101 (for mypy)
                # Always convert anything nested to Tap-Tachyon regardless of the top level.
                converted.arguments[param.name] = _to_tap_tachyon_helper(arg, _schema_to_tap)
            else:
                converted.arguments[param.name] = type_info.arguments[param.name]
        if isinstance(type_info.instantiates, schema.ResolvedSchema):
            return schema_transform(converted)
        return converted
    return type_info


def _to_tachyon(type_info: typesys.TypeVal) -> typesys.TypeVal:
    """Convert the entire tree to tap-tachyon and leave the top-level as tachyon."""
    return _to_tap_tachyon_helper(type_info, _schema_to_tachyon)


def _to_tap_tachyon(type_info: typesys.TypeVal) -> typesys.TypeVal:
    """Convert the entire tree to tap-tachyon."""
    return _to_tap_tachyon_helper(type_info, _schema_to_tap)


def _to_tappy(type_info: typesys.TypeVal) -> typesys.TypeVal:
    """Convert the entire tree to tap-tachyon and leave the top-level as tappy."""
    return _to_tap_tachyon_helper(type_info, _schema_to_tappy)


def _to_tap_init(type_info: typesys.TypeVal) -> typesys.TypeVal:
    """Convert the entire tree to tap-tachyon and leave the top-level as tap-init."""
    return _to_tap_tachyon_helper(type_info, _schema_to_tap_init)


def _schema_to_tachyon(schema_ir: schema.ResolvedSchema | typesys.Instantiation) -> typesys.Instantiation:
    """Convert a schema type to a Tachyon type."""
    return typesys.Instantiation(
        instantiates=clkbuiltins.TACHYON,
        arguments={"schema": schema_ir},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def _schema_to_tap(schema_ir: schema.ResolvedSchema | typesys.Instantiation) -> typesys.Instantiation:
    """Convert a schema type to a Tap type."""
    return typesys.Instantiation(
        instantiates=clkbuiltins.TAP,
        arguments={"representation": _schema_to_tachyon(schema_ir)},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def _schema_to_tappy(schema_ir: schema.ResolvedSchema | typesys.Instantiation) -> typesys.Instantiation:
    """Convert a schema type to a Tappy type."""
    return typesys.Instantiation(
        instantiates=clkbuiltins.TAPPY,
        arguments={"schema": schema_ir},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def _schema_to_tap_init(schema_ir: schema.ResolvedSchema | typesys.Instantiation) -> typesys.Instantiation:
    """Convert a schema type to a Tap type."""
    return typesys.Instantiation(
        instantiates=clkbuiltins.TAP_INIT,
        arguments={"representation": _schema_to_tachyon(schema_ir)},
        type_info=clkbuiltins.TYPE_TYPE,
    )


def create_get_span_method(
    field_name: str, cpp_type: types.CppType | types.CppTemplateType, const: bool
) -> types.CppMethod:
    """Define a const get method that returns a span."""
    inline = CppChunk()
    return_value = types.CppValue(cpp_type, f"fields_.{field_name}").render(types.GLOBAL_NAMESPACE)
    inline.append(f"return {return_value};")
    inline.context.add_includes(cpp_type.includes)
    name = f"get_{field_name}" if const else f"get_mutable_{field_name}"
    return types.CppMethod(
        name=name,
        doc=f"{field_name}: Get a {'const ' if const else ''}span.",
        return_type=cpp_type,
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=(["const"] if const else []) + [types.Ref.L.value],
        body=inline,
        no_discard=True,
    )


def create_set_span_method(field_name: str, cpp_type: types.CppType | types.CppTemplateType) -> types.CppMethod:
    """Define a const get method that returns a span."""
    inline = CppChunk()
    inline.append(f"std::copy(input_span.begin(), input_span.end(), ::std::begin(fields_.{field_name}));")
    inline.context.add_includes(cpp_type.includes)
    inline.context.add_includes((types.ALGORITHM_HEADER, types.ITERATOR_HEADER))
    return types.CppMethod(
        name=f"set_{field_name}",
        doc=f"{field_name}: Set from a span.",
        return_type=types.VOID,
        arguments=[types.CppNamedType(cpp_type, "input_span")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[types.Ref.L.value],
        body=inline,
        no_discard=False,
    )


def create_value_optional_method(field_name: str, cpp_type: types.CppTypeExpr, const: bool) -> types.CppMethod:
    """Define a get method for an optional."""
    inline = CppChunk()
    inline.append("// Throws if no value exists.")
    inline.append(f"return fields_.{field_name}.value();")
    name = ("value_" if const else "value_mutable_") + field_name
    return types.CppMethod(
        name=name,
        doc=f"{field_name}: Get the value of an optional by reference.",
        return_type=cpp_type,
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=(["const"] if const else []) + [types.Ref.L.value],
        body=inline,
        no_discard=True,
    )


def create_set_optional_method(field_name: str, cpp_type: types.CppTypeExpr) -> types.CppMethod:
    """Define a set method for an optional."""
    inline = CppChunk()
    inline.append(f"fields_.{field_name}.emplace(new_value);")
    return types.CppMethod(
        name=f"set_{field_name}",
        doc=f"{field_name}: Set the optional to the input value.",
        return_type=types.VOID,
        arguments=[types.CppNamedType(cpp_type, "new_value")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[types.Ref.L.value],
        body=inline,
        no_discard=False,
    )


def create_reset_optional_method(field_name: str) -> types.CppMethod:
    """Define a reset method for an optional."""
    inline = CppChunk()
    inline.append(f"fields_.{field_name}.reset();")
    return types.CppMethod(
        name=f"reset_{field_name}",
        doc=f"{field_name}: Resets the optional to std::nullopt.",
        return_type=types.VOID,
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=[types.Ref.L.value],
        body=inline,
        no_discard=False,
    )


def create_has_optional_method(field_name: str) -> types.CppMethod:
    """Define a method to check if an optional is holding a value."""
    inline = CppChunk()
    inline.append(f"return fields_.{field_name}.has_value();")
    context = CompilerContext()
    return types.CppMethod(
        name=f"has_{field_name}",
        doc=f"{field_name}: Check if the optional has a value.",
        return_type=typereg.get_cpp_type(context, clkbuiltins.BOOL),
        arguments=[],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const", types.Ref.L.value],
        body=inline,
        no_discard=True,
    )


def _field_fixed_array_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert a fixed array to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info

    if not isinstance(field_type, typesys.Instantiation) or field_type.instantiates != clkbuiltins.FIXED_ARRAY:
        msg = f"Expected a FixedArray field type.  Received: {field_type}"
        raise TypeError(msg)

    value_type = field_type.arguments["type"]
    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal.  Received: {value_type}."
        raise TypeError(msg)

    size_value = field_type.arguments["size"]

    cpp_size = _value_to_cpp(size_value)
    if not isinstance(cpp_size, types.CppValue):
        msg = f"Expected a CppValue for the size argument.  Received: {cpp_size}"
        raise TypeError(msg)
    cpp_value_type = _to_cpp_type(compiler_context, value_type)
    span = types.SPAN.instantiate([types.const_qualify(cpp_value_type, True), cpp_size])
    mutable_span = types.SPAN.instantiate([cpp_value_type, cpp_size])

    cpp_type = _to_cpp_type(compiler_context, field_type)

    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(field.init_value),
            doc=f"{field.cur_name}: data member.",
        ),
        methods=[
            create_get_span_method(field.cur_name, span, True),
            create_get_span_method(field.cur_name, mutable_span, False),
            create_set_span_method(field.cur_name, span),
        ],
    )


def _field_var_array_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert a var array to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info

    if not isinstance(field_type, typesys.Instantiation) or field_type.instantiates != clkbuiltins.VAR_ARRAY:
        msg = f"Expected a VarArray field type.  Received: {field_type}"
        raise TypeError(msg)

    value_type = field_type.arguments["type"]
    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal.  Received: {value_type}."
        raise TypeError(msg)

    cpp_value_type = _to_cpp_type(compiler_context, value_type)
    span = types.SPAN.instantiate([types.const_qualify(cpp_value_type, True)])
    mutable_span = types.SPAN.instantiate([cpp_value_type])

    cpp_type = _to_cpp_type(compiler_context, field_type)

    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(field.init_value),
            doc=f"{field.cur_name}: data member.",
        ),
        methods=[
            create_get_span_method(field.cur_name, span, True),
            create_get_span_method(field.cur_name, mutable_span, False),
            create_get_method(field.cur_name, types.cref_qualify(cpp_type, True, types.Ref.L), "get_underlying"),
            create_get_mutable_method(field.cur_name, "get_underlying", types.ref_qualify(cpp_type, types.Ref.L)),
        ],
    )


def _field_var_string_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert a var string to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info

    if not isinstance(field_type, typesys.Instantiation) or field_type.instantiates != clkbuiltins.VAR_STRING:
        msg = f"Expected a VarString field type.  Received: {field_type}"
        raise TypeError(msg)

    mutable_span = types.SPAN.instantiate([types.CHAR])

    cpp_type = _to_cpp_type(compiler_context, field_type)

    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(field.init_value),
            doc=f"{field.cur_name}: data member.",
        ),
        methods=[
            create_get_span_method(field.cur_name, types.STRING_VIEW, True),
            create_get_span_method(field.cur_name, mutable_span, False),
            create_get_method(field.cur_name, types.cref_qualify(cpp_type, True, types.Ref.L), "get_underlying"),
            create_get_mutable_method(field.cur_name, "get_underlying", types.ref_qualify(cpp_type, types.Ref.L)),
        ],
    )


def _field_optional_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert an optional to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info

    if not isinstance(field_type, typesys.Instantiation) or field_type.instantiates != clkbuiltins.OPTIONAL:
        msg = f"Expected an Optional field type.  Received: {field_type}"
        raise TypeError(msg)

    cpp_type = _to_cpp_type(compiler_context, field_type)

    value_type = field_type.arguments["type"]
    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal.  Received: {value_type}."
        raise TypeError(msg)

    cpp_value_type = _to_cpp_type(compiler_context, value_type)

    l_value = types.ref_qualify(cpp_value_type, types.Ref.L)
    const_l_value = types.const_qualify(l_value, True)

    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(field.init_value),
            doc=f"{field.cur_name}: data member.",
        ),
        methods=[
            create_value_optional_method(field.cur_name, const_l_value, True),
            create_value_optional_method(field.cur_name, l_value, False),
            create_set_optional_method(field.cur_name, const_l_value),
            create_reset_optional_method(field.cur_name),
            create_has_optional_method(field.cur_name),
        ],
    )


def _field_instantiation_to_cpp(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef
) -> types.CppField:
    """Convert an instantiated type to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info
    if not isinstance(field_type, typesys.Instantiation | schema.InstantiatedSchema):
        msg = f"Unexpected field type.  Received: {type(field_type)}"
        raise TypeError(msg)

    cpp_type = _to_cpp_type(compiler_context, field_type)

    l_value = types.ref_qualify(cpp_type, types.Ref.L)
    const_l_value = types.const_qualify(l_value, True)

    return types.CppField(
        doc=field.doc.value,
        member=types.CppNamedValue(
            named_type=types.CppNamedType(cpp_type, field.cur_name),
            value=_value_to_cpp(field.init_value),
            doc=f"{field.cur_name}: data member.",
        ),
        methods=[
            create_get_method(field.cur_name, const_l_value),
            create_get_mutable_method(field.cur_name, "get_mutable", l_value),
            create_set_method(field.cur_name, const_l_value),
        ],
    )


def _field_to_cpp(compiler_context: CompilerContext, field: schema.InstantiatedFieldDef) -> types.CppField:
    """Convert the field to a cpp implementation.

    Args:
        compiler_context: The compiler context.
        field: The field to convert.

    Return: A cpp implementation.

    """
    field_type = field.type_info
    if isinstance(field_type, typesys.TypeVal) and _treat_as_primitive(field_type):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return _field_primitive_to_cpp(compiler_context, field)
    if isinstance(field_type, typesys.Instantiation) and field_type.instantiates == clkbuiltins.FIXED_ARRAY:
        return _field_fixed_array_to_cpp(compiler_context, field)
    if isinstance(field_type, typesys.Instantiation) and field_type.instantiates == clkbuiltins.VAR_ARRAY:
        return _field_var_array_to_cpp(compiler_context, field)
    if isinstance(field_type, typesys.Instantiation) and field_type.instantiates == clkbuiltins.VAR_STRING:
        return _field_var_string_to_cpp(compiler_context, field)
    if isinstance(field_type, typesys.Instantiation) and field_type.instantiates == clkbuiltins.OPTIONAL:
        return _field_optional_to_cpp(compiler_context, field)
    if isinstance(field_type, typesys.Instantiation | schema.InstantiatedSchema):
        return _field_instantiation_to_cpp(compiler_context, field)
    msg = f"Unable to convert type {field_type.value_key()} to CppField."
    raise TypeError(msg)


def to_template_param(compiler_context: CompilerContext, param: typesys.Parameter) -> types.CppNamedType:
    """Convert a schema parameter into a Cpp template parameter."""
    if param.type_bound is clkbuiltins.TYPE_TYPE:
        return types.CppTypeArg(param.name)
    return types.CppNamedType(typereg.get_cpp_type(compiler_context, param.type_bound), param.name)


def to_cpp_struct(compiler_context: CompilerContext, type_info: typesys.TypeDef) -> types.CppStruct:
    """Convert a TypeDef into a CppStruct declaration."""
    if isinstance(type_info, strongtypes.Tag):
        cpp_struct = types.CppStruct(
            name=typereg.get_cpp_type(compiler_context, type_info),
            doc=type_info.doc.value,
        )
    elif isinstance(type_info, schema.Schema):
        cpp_struct = types.CppStruct(
            name=types.CppType([], type_info.name, None),
            doc=type_info.doc.value,
        )
        generic_parameters = type_info.generic_parameters()
        if generic_parameters:
            cpp_struct.template_param.extend(to_template_param(compiler_context, param) for param in generic_parameters)
    else:
        msg = f"Unable to support conversion to CppStruct from: {type(type_info)}"
        raise TypeError(msg)

    if hasattr(type_info, "uuid") and type_info.uuid is not None:  # pyright: ignore[reportAttributeAccessIssue] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        cpp_struct.public.append(
            values.uuid_to_named_value(compiler_context, type_info.uuid, "uuid", tag=clkbuiltins.SCHEMA_TAG_TYPE)  # pyright: ignore[reportAttributeAccessIssue] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        )

    return cpp_struct


def define_default_comparison_operator(class_type: types.CppType | types.CppTemplateType) -> types.CppMethod:
    """Define a defaulted comparison operator."""
    const_ref_class = types.const_qualify(types.ref_qualify(class_type, types.Ref.L), True)
    context = CompilerContext()
    return types.CppMethod(
        name="operator==",
        doc="Equality operator.",
        return_type=typereg.get_cpp_type(context, clkbuiltins.BOOL),
        arguments=[types.CppNamedType(const_ref_class, "other")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const", "= default"],
        body=None,
        no_discard=True,
    )


def define_comparison_operator(
    class_type: types.CppType | types.CppTemplateType,
    field_names: list[str],
) -> types.CppMethod:
    """Define a comparison operator comparing fields."""
    const_ref_class = types.const_qualify(types.ref_qualify(class_type, types.Ref.L), True)

    inline = CppChunk()
    if len(field_names) == 0:
        inline.append("return true;")
    elif len(field_names) == 1:
        name = field_names[0]
        inline.append(f"return {name} == other.{name};")
    else:
        first = field_names[0]
        inline.append(f"return {first} == other.{first}")
        for index in range(1, len(field_names) - 1):
            name = field_names[index]
            inline.append(f"&& {name} == other.{name}", indent=1)
        last = field_names[-1]
        inline.append(f"&& {last} == other.{last};", indent=1)

    return types.CppMethod(
        name="operator==",
        doc="Equality operator.",
        # Safe to use fresh context here because BOOL is a built-in
        return_type=typereg.get_cpp_type(CompilerContext(), clkbuiltins.BOOL),
        arguments=[types.CppNamedType(const_ref_class, "other")],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["const"],
        body=inline,
        no_discard=True,
    )


def define_default_constructor() -> types.CppConstructor:
    """Define a default constructor."""
    return types.CppConstructor(
        doc="Default constructor.",
        arguments=[],
        leading_qualifiers=[],
        trailing_qualifiers=["=", "default"],
        member_init_list=None,
        body=None,
    )


def define_tap_init_constructor(
    init_type: types.CppTypeExpr,
    field_names: list[str],
) -> types.CppConstructor:
    """Define a constructor that takes in the TapInit struct."""
    init_var = "init"
    fields_init = "\n".join([f".{name} = {init_var}.{name}," for name in field_names])

    return types.CppConstructor(
        doc="Constructor from init struct.",
        arguments=[types.CppNamedType(types.ref_qualify(types.const_qualify(init_type, True), types.Ref.L), init_var)],
        leading_qualifiers=["inline"],
        trailing_qualifiers=["noexcept"],
        member_init_list=[("fields_", fields_init)],
        body=None,
        implicit=True,
    )


def create_padding_array(name: str, size: int) -> types.CppNamedValue:
    """Create padding array."""
    context = CompilerContext()
    padding_type = typereg.get_cpp_type(
        context,
        typesys.Instantiation(
            instantiates=clkbuiltins.FIXED_ARRAY,
            arguments={"type": clkbuiltins.BYTE, "size": primitive.DecimalValue(clkbuiltins.UINT64, Decimal(size))},
            type_info=clkbuiltins.TYPE_TYPE,
        ),
    )
    return types.CppNamedValue(
        named_type=types.CppNamedType(
            argument_type=padding_type,
            argument_name=name,
        ),
        value=types.CppValue(None, ""),
        doc="Padding array.",
    )


@dataclass
class SchemaTagTap:
    """Generate the Schema Tag type in C++."""

    schema_ir: schema.Schema

    def render(self, enclosing_namespace: str) -> CppModuleChunks:
        """Render to C++.

        Args:
            enclosing_namespace: Namespace in which the Schema Tag is being defined.
        """
        cpp_mod = CppModuleChunks()
        cpp_mod.header_chunk.append(
            [
                *comment_doc_string(self.schema_ir.doc.value),
                f"struct {self.schema_ir.name}",
                "{",
            ],
        )

        if self.schema_ir.uuid is not None:
            uuid = values.uuid_to_named_value(
                self.schema_ir.module.context, self.schema_ir.uuid, "uuid", tag=clkbuiltins.SCHEMA_TAG_TYPE
            )
            cpp_mod.header_chunk.context.add_includes(uuid.includes)
            cpp_mod.header_chunk.append(uuid.render(enclosing_namespace), indent=1)

        cpp_mod.header_chunk.append("};")
        return cpp_mod


def to_schema_instantiation(typespec: typesys.Instantiation) -> schema.InstantiatedSchema:
    """Extract the instantiated schema type from a Tap or a Tachyon instantiation."""
    if typespec.instantiates is clkbuiltins.TAP:
        representation = typespec.arguments["representation"]
        if (
            not isinstance(representation, typesys.Instantiation)
            or representation.instantiates is not clkbuiltins.TACHYON
        ):
            msg = f"Expected an instantiation of Tachyon.  Received: {representation}"
            raise TypeError(msg)
        typespec = representation
    if typespec.instantiates is clkbuiltins.TACHYON:
        schema_ir = typespec.arguments["schema"]
        assert isinstance(schema_ir, schema.Schema | typesys.Instantiation)  # noqa: S101  (invariant)
        return schema.InstantiatedSchema.from_typespec(schema_ir)
    msg = f"Expected an instantiation of either Tap or Tachyon.  Received: {type(typespec)}"
    raise TypeError(msg)


@dataclass
class CppFieldDef:
    """A resolved schema field."""

    offset: int
    size: int
    field_num: int
    field_name: str
    field: types.CppField


@dataclass
class CppFieldGap:
    """A resolved schema field."""

    offset: int
    size: int
    field_name: str
    field: types.CppField


def to_cpp_fields(
    compiler_context: CompilerContext, schema_ir: schema.InstantiatedSchema, layout: Layout
) -> tuple[list[CppFieldDef], list[CppFieldGap]]:
    """Convert a schema and a layout to a list of Cpp field definitions."""
    converted_fields = []
    for layout_field in layout.fields:
        schema_field = schema_ir.fields[layout_field.field_num]
        converted_fields.append(
            CppFieldDef(
                offset=layout_field.offset,
                size=layout_field.size,
                field_num=layout_field.field_num,
                field_name=schema_field.cur_name,
                field=_field_to_cpp(compiler_context, schema_field),
            ),
        )

    gap_fields = []
    for index, gap in enumerate(layout.gaps):
        gap_name = f"padding_{index}_"
        gap_fields.append(
            CppFieldGap(
                offset=gap.offset,
                size=gap.size,
                field_name=gap_name,
                field=types.CppField(
                    doc="Padding",
                    member=create_padding_array(gap_name, gap.size),
                    methods=[],
                ),
            ),
        )

    return converted_fields, gap_fields


def to_class_local_defs(
    context: CompilerContext,
    parameters: list[typesys.Parameter],
    arguments: Mapping[str, typesys.Value],
) -> list[types.CppNamedValue | types.CppTypeAliasDef]:
    """Convert template parameters to class local type aliases and static constexpr variables."""
    defs: list[types.CppNamedValue | types.CppTypeAliasDef] = []
    for param in parameters:
        if param.type_bound is clkbuiltins.TYPE_TYPE:
            arg = arguments[param.name]
            if not isinstance(arg, typesys.TypeVal):
                msg = f"A TYPE_TYPE parameter is not bound to a TalVal argument.  Bound to: {type(arg)}"
                raise TypeError(msg)
            defs.append(
                types.CppTypeAliasDef(
                    name=param.name,
                    alias_for=_to_cpp_type(context, arg),
                    doc=f"Parameter: {param.name}",
                ),
            )
        else:
            defs.append(
                types.CppNamedValue(
                    named_type=types.CppNamedType(
                        argument_type=typereg.get_cpp_type(context, param.type_bound),
                        argument_name=param.name,
                    ),
                    value=_value_to_cpp(arguments[param.name]),
                    doc=f"Parameter: {param.name}",
                    qualifiers=["static", "constexpr"],
                ),
            )
    return defs


@dataclass
class CppSpec:
    """All data needed to render a schema as Tap and Tachyon."""

    compiler_context: CompilerContext
    typespec: typesys.Instantiation
    schema_ir: schema.InstantiatedSchema
    layout: Layout
    class_local_defs: list[types.CppNamedValue | types.CppTypeAliasDef]
    field_defs: list[CppFieldDef]
    field_gaps: list[CppFieldGap]


def to_cpp_spec(compiler_context: CompilerContext, typespec: typesys.Instantiation) -> CppSpec:
    """Convert a schema typespec to a CppSpec."""
    schema_ir = to_schema_instantiation(typespec)
    layout = layout_schema(compiler_context, schema_ir)
    if schema_ir.arguments is not None:
        parameters = schema_ir.schema.generic_parameters()
        assert parameters is not None  # noqa: S101  (invariant)
        class_local_defs = to_class_local_defs(compiler_context, parameters, schema_ir.arguments)
    else:
        class_local_defs = []

    field_defs, field_gaps = to_cpp_fields(compiler_context, schema_ir, layout)

    return CppSpec(
        compiler_context=compiler_context,
        typespec=typespec,
        schema_ir=schema_ir,
        layout=layout,
        class_local_defs=class_local_defs,
        field_defs=field_defs,
        field_gaps=field_gaps,
    )


def render_tap_init_struct(cpp_spec: CppSpec, enclosing_namespace: str) -> CppModuleChunks:
    """Render a Tap init struct from a CppSpec."""
    fields = cpp_spec.field_defs

    schema_name = cpp_spec.schema_ir.schema_name
    field_src_order = cpp_spec.schema_ir.field_src_order

    tap_init_type = types.CppStruct(
        name=typereg.get_cpp_type(cpp_spec.schema_ir.schema.module.context, _to_tap_init(cpp_spec.schema_ir)),
        doc=f"Tap init struct for {schema_name}.",
        no_lints=["clang-analyzer-optin.performance.Padding"],
    )
    tap_init_type.public.extend(cpp_spec.class_local_defs)

    for field in sorted(fields, key=lambda field: field_src_order[field.field_num]):
        tap_init_type.public.append(field.field.member)

    cpp_mod = CppModuleChunks()

    cpp_mod.append(tap_init_type.render(enclosing_namespace))

    tap_init_fqn = tap_init_type.name.render(enclosing_namespace)
    impl_chunk = cpp_mod.implementation_chunk
    for elem in fields:
        field_name = elem.field_name
        impl_chunk.append(f"static_assert(sizeof(::std::declval<{tap_init_fqn}>().{field_name}) == {elem.size});")

    impl_chunk.context.add_include(typereg.META_CONCEPTS_HEADER)
    impl_chunk.append(f"static_assert(::jewels::meta::ImplicitLifetimeType<{tap_init_fqn}>);")

    return cpp_mod


def _render_logging_traits(
    compiler_context: CompilerContext,
    schema_name: str,
    cpp_spec: CppSpec,
    tachyon_type: types.CppStruct,
    enclosing_namespace: str,
) -> CppModuleChunks:
    """Render the template specialization for the logging traits, including schema_name and schema_definition."""
    specialized_struct = f"clockwork::LoggingTraits<{tachyon_type.name.render(enclosing_namespace)}>"
    binary_schema = tachyon_metadata.get_serialized_metadata(compiler_context, cpp_spec.schema_ir)

    cpp_mod = CppModuleChunks()

    # header
    header_chunk = cpp_mod.header_chunk
    header_chunk.context.add_include(SystemHeader("string_view"))
    header_chunk.context.add_include(typereg.REPR_IFACE_HEADER)
    header_chunk.append(f"// Tachyon logging traits for {schema_name}.")
    header_chunk.append("template <>")
    header_chunk.append(f"struct ::{specialized_struct}")
    header_chunk.append("  : public ::clockwork::TachyonLoggingTraits")
    header_chunk.append("{")
    header_chunk.append("  static const std::string_view schema_name;")
    header_chunk.append(f"  static const std::array<char, {len(binary_schema)}> schema_definition;")
    header_chunk.append("};")

    # implementation
    impl_chunk = cpp_mod.implementation_chunk
    impl_chunk.append(f"const std::string_view {specialized_struct}::schema_name = ")
    impl_chunk.append(f'    "{cpp_spec.schema_ir.value_key()}";')
    impl_chunk.append(f"const std::array<char, {len(binary_schema)}> {specialized_struct}::schema_definition = {{")
    array_line = "   "
    count = 0
    for hex_char in map(hex, tachyon_metadata.get_serialized_metadata(compiler_context, cpp_spec.schema_ir)):
        array_line += f" '\\x{hex_char[2:]}',"
        count += 1
        if count == 12:  # noqa: PLR2004
            impl_chunk.append(array_line)
            array_line = "   "
            count = 0
    if count != 0:
        impl_chunk.append(array_line)
    impl_chunk.append("};")

    return cpp_mod


def render_representation(
    compiler_context: CompilerContext, cpp_spec: CppSpec, enclosing_namespace: str
) -> CppModuleChunks:
    """Render a Tachyon representation from a CppSpec."""
    offset_order = cpp_spec.field_defs + cpp_spec.field_gaps
    offset_order.sort(key=lambda field: field.offset)

    schema_name = cpp_spec.schema_ir.schema_name
    tachyon_type = types.CppStruct(
        name=typereg.get_cpp_type(cpp_spec.compiler_context, _to_tachyon(cpp_spec.schema_ir)),
        doc=f"Tachyon layout for {schema_name}.",
        attributes=["__attribute__((__packed__))", f"alignas({cpp_spec.layout.alignment})"],
    )
    tachyon_type.public.extend(cpp_spec.class_local_defs)

    tachyon_type.public.append(
        values.uuid_to_named_value(
            cpp_spec.compiler_context,
            lookup_uuid(compiler_context, cpp_spec.typespec.arguments["representation"]),
            "_clockwork_uuid",
            tag=clkbuiltins.REPRESENTATION_TAG_TYPE,
        )
    )

    for field in offset_order:
        tachyon_type.public.append(field.field.member)

    tachyon_type.public.append(
        define_comparison_operator(
            tachyon_type.name,
            [field.field_name for field in offset_order if isinstance(field, CppFieldDef)],
        )
    )

    cpp_mod = CppModuleChunks()

    header_chunk = cpp_mod.header_chunk
    header_chunk.append(["#pragma clang diagnostic push", '#pragma clang diagnostic ignored "-Wpacked-non-pod"'])
    cpp_mod.append(tachyon_type.render(enclosing_namespace))
    header_chunk.append("#pragma clang diagnostic pop")

    tachyon_fqn = tachyon_type.name.render(enclosing_namespace)
    impl_chunk = cpp_mod.implementation_chunk
    for elem in offset_order:
        field_name = elem.field_name
        impl_chunk.append(f"static_assert(offsetof(decltype({tachyon_fqn}{{}}), {field_name}) == {elem.offset});")
        impl_chunk.append(f"static_assert(sizeof(::std::declval<{tachyon_fqn}>().{field_name}) == {elem.size});")

    impl_chunk.append(f"static_assert(sizeof({tachyon_fqn}) == {cpp_spec.layout.size});")
    impl_chunk.append(f"static_assert(alignof({tachyon_fqn}) == {cpp_spec.layout.alignment});")

    impl_chunk.context.add_include(typereg.META_CONCEPTS_HEADER)
    impl_chunk.append(f"static_assert(::jewels::meta::ImplicitLifetimeType<{tachyon_fqn}>);")

    if cpp_spec.schema_ir.schema_uuid:
        cpp_mod.append(
            _render_logging_traits(compiler_context, schema_name, cpp_spec, tachyon_type, enclosing_namespace)
        )

    return cpp_mod


def render_interface(compiler_context: CompilerContext, cpp_spec: CppSpec, enclosing_namespace: str) -> CppModuleChunks:
    """Render a Tap interface from a CppSpec."""
    if enclosing_namespace != types.GLOBAL_NAMESPACE:
        msg = f"Interfaces must be defined in the global namespace.  Current namespace: {enclosing_namespace}"
        raise ValueError(msg)

    schema_name = cpp_spec.schema_ir.schema_name
    tap_type = types.CppStruct(
        name=typereg.get_cpp_type(compiler_context, _to_tap_tachyon(cpp_spec.schema_ir)),
        doc=f"Tap interface for the Tachyon representation of {schema_name}.",
    )
    tap_type.public.extend(cpp_spec.class_local_defs)

    offset_order = cpp_spec.field_defs + cpp_spec.field_gaps
    offset_order.sort(key=lambda field: field.offset)

    tap_public_members = tap_type.members.setdefault(types.MemberAccess.public, [])
    tap_public_members.append(define_default_constructor())

    schema_options = cpp_spec.schema_ir.options
    if schema_options and schema_options.provide_constructor:
        tap_public_members.append(
            define_tap_init_constructor(
                typereg.get_cpp_type(compiler_context, _to_tap_init(cpp_spec.schema_ir)),
                # Need to construct in offset order so the Tachyon layout struct is initialized in the right order.
                [field.field_name for field in offset_order if isinstance(field, CppFieldDef)],
            )
        )
    for field_def in cpp_spec.field_defs:
        tap_public_members.extend(field_def.field.methods)

    fields_var_name = "fields_"

    tap_type.public.append(define_comparison_operator(tap_type.name, [fields_var_name]))

    tachyon_type = typereg.get_cpp_type(compiler_context, _to_tachyon(cpp_spec.schema_ir))
    tap_type.private.append(
        types.CppNamedValue(
            named_type=types.CppNamedType(argument_type=tachyon_type, argument_name=fields_var_name),
            value=types.CppValue(None, ""),
            doc="Data member layout struct.",
        ),
    )

    cpp_mod = CppModuleChunks()

    cpp_mod.append(tap_type.render(enclosing_namespace))

    tap_fqn = tap_type.name.render(enclosing_namespace)
    impl_chunk = cpp_mod.implementation_chunk
    impl_chunk.append(
        f"static_assert(sizeof({tap_fqn}) == {cpp_spec.layout.size});",
    )
    impl_chunk.append(
        f"static_assert(alignof({tap_fqn}) == {cpp_spec.layout.alignment});",
    )
    impl_chunk.context.add_include(typereg.META_CONCEPTS_HEADER)
    impl_chunk.append(f"static_assert(::jewels::meta::ImplicitLifetimeType<{tap_fqn}>);")

    tappy_alias = typereg.get_cpp_type(compiler_context, _to_tappy(cpp_spec.schema_ir))
    tappy_alias_fqn = tappy_alias.render(enclosing_namespace)
    impl_chunk.context.add_include(typereg.TYPE_TRAITS_HEADER)
    impl_chunk.append(f"static_assert(::std::is_same<{tappy_alias_fqn},{tap_fqn}>::value);")

    return cpp_mod


def get_tap_tachyon_cpp_type(
    compiler_context: CompilerContext, typespec: typesys.Instantiation
) -> types.CppType | types.CppTemplateType:
    """Get the C++ type that corresponds to the given Tap interface and Tachyon representation."""
    cpp_spec = to_cpp_spec(compiler_context, typespec)
    return typereg.get_cpp_type(compiler_context, _to_tap_tachyon(cpp_spec.schema_ir))


def render(
    compiler_context: CompilerContext, typespec: typesys.Instantiation, enclosing_namespace: str
) -> CppModuleChunks:
    """Render a Tap interface and Tachyon representation."""
    cpp_mod = CppModuleChunks()
    cpp_spec = to_cpp_spec(compiler_context, typespec)
    schema_options = cpp_spec.schema_ir.options
    if schema_options and schema_options.provide_constructor:
        cpp_mod.append(render_tap_init_struct(cpp_spec, enclosing_namespace))
    cpp_mod.append(render_representation(compiler_context, cpp_spec, enclosing_namespace))
    cpp_mod.append(render_interface(compiler_context, cpp_spec, enclosing_namespace))
    return cpp_mod


def render_alias(
    compiler_context: CompilerContext, typespec: typesys.Instantiation, enclosing_namespace: str, alias_to: str
) -> CppModuleChunks:
    """Render the alias for the interface."""
    cpp_mod = CppModuleChunks()
    interface_alias = types.CppTypeAliasDef(
        name=alias_to,
        alias_for=typereg.get_cpp_type(compiler_context, _to_tap_tachyon(to_schema_instantiation(typespec))),
        doc=None,
    )
    cpp_mod.header_chunk.append(interface_alias.render(enclosing_namespace))

    return cpp_mod
