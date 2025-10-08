# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Render a nanobind target to C++ nanobind module definition."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Literal, TypeGuard

from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.cpp.context import Header, SystemHeader
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    nanobinding_registry,
    primitive,
    schema,
    typesys,
)
from clockwork.dsl.ir.clkbuiltins import PrimitiveType
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO, ModuleID
from clockwork.dsl.ir.strongtypes import StrongType
from clockwork.dsl.ir.typesys import TypeVal
from clockwork.dsl.serialization import tachyon_reg
from clockwork.dsl.serialization.tap import (
    _to_cpp_type,  # pyright: ignore[reportPrivateUsage] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
    to_schema_instantiation,
)
from typing_extensions import override

if TYPE_CHECKING:
    from collections.abc import Callable

    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir.expr import TypeExpression
    from clockwork.dsl.ir.nanobind_binding import ResolvedNanobindBinding
    from clockwork.dsl.ir.nanobinding_registry import TargetId
    from clockwork.dsl.ir.statement import ImmutableBinding


def _get_optional_element_type(type_val: TypeVal) -> TypeVal:
    """Optional<T> -> T."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != clkbuiltins.OPTIONAL:
        msg = f"Expected an Optional field type. Received: {type_val}"
        raise TypeError(msg)

    element_type = type_val.arguments["type"]
    if not isinstance(element_type, TypeVal):
        msg = f"Expected a TypeVal. Received: {element_type}."
        raise TypeError(msg)

    return element_type


def _get_fixed_array_element_type(type_val: TypeVal) -> TypeVal:
    """FixedArray<T, size> -> T."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != clkbuiltins.FIXED_ARRAY:
        msg = f"Expected a FixedArray field type. Received: {type_val}"
        raise TypeError(msg)

    element_type = type_val.arguments["type"]
    if not isinstance(element_type, TypeVal):
        msg = f"Expected a TypeVal. Received: {element_type}."
        raise TypeError(msg)

    return element_type


def _get_var_array_element_type(type_val: TypeVal) -> TypeVal:
    """VarArray<T, max_size> -> T."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != clkbuiltins.VAR_ARRAY:
        msg = f"Expected a VarArray field type. Received: {type_val}"
        raise TypeError(msg)

    element_type = type_val.arguments["type"]
    if not isinstance(element_type, TypeVal):
        msg = f"Expected a TypeVal. Received: {element_type}."
        raise TypeError(msg)

    return element_type


def _get_fixed_array_size(type_val: TypeVal) -> int:
    """FixedArray<T, size> -> size."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != clkbuiltins.FIXED_ARRAY:
        msg = f"Expected a FixedArray type. Received: {type_val}"
        raise TypeError(msg)

    size = type_val.arguments["size"]
    if not isinstance(size, primitive.DecimalLiteral):
        err = f"size is not a DecimalLiteral, it's {size}"
        raise TypeError(err)
    return primitive.decimal_to_int(size)


def _get_var_array_max_size(type_val: TypeVal) -> int:
    """VarArray<T, max_size> -> max_size."""
    if not isinstance(type_val, typesys.Instantiation) or type_val.instantiates != clkbuiltins.VAR_ARRAY:
        msg = f"Expected a VarArray type. Received: {type_val}"
        raise TypeError(msg)

    max_size = type_val.arguments["max_size"]
    if not isinstance(max_size, primitive.DecimalLiteral):
        err = f"max_size is not a DecimalLiteral, it's {max_size}"
        raise TypeError(err)
    return primitive.decimal_to_int(max_size)


def _get_var_string_max_size(type_val: TypeVal) -> int:
    """Get the max size parameter from a VarString as an int."""
    assert _is_var_string_type(type_val)
    max_size = type_val.arguments["max_size"]
    assert isinstance(max_size, primitive.DecimalLiteral)
    return int(max_size.value)


def _is_var_array_type(type_val: TypeVal) -> TypeGuard[typesys.Instantiation]:
    """Check if this type is a VarArray."""
    return isinstance(type_val, typesys.Instantiation) and type_val.instantiates == clkbuiltins.VAR_ARRAY


def _is_fixed_array_type(type_val: TypeVal) -> TypeGuard[typesys.Instantiation]:
    """Check if this type is a FixedArray."""
    return isinstance(type_val, typesys.Instantiation) and type_val.instantiates == clkbuiltins.FIXED_ARRAY


def _is_bytes_array(type_val: TypeVal) -> TypeGuard[typesys.Instantiation]:
    """Check if this type an array of bytes (Fixed or Var)."""
    if _is_fixed_array_type(type_val):
        elem_type = _get_fixed_array_element_type(type_val)
        return elem_type == clkbuiltins.BYTE
    if _is_var_array_type(type_val):
        elem_type = _get_var_array_element_type(type_val)
        return elem_type == clkbuiltins.BYTE
    return False


def _is_optional_type(type_val: TypeVal) -> TypeGuard[typesys.Instantiation]:
    """Check if this type is an Optional."""
    return isinstance(type_val, typesys.Instantiation) and type_val.instantiates == clkbuiltins.OPTIONAL


def _is_var_string_type(type_val: TypeVal) -> TypeGuard[typesys.Instantiation]:
    """Check if this type is a VarString."""
    return isinstance(type_val, typesys.Instantiation) and type_val.instantiates == clkbuiltins.VAR_STRING


_BUILT_IN_CONVERSIONS = [clkbuiltins.DURATION, clkbuiltins.SYNC_TIME, clkbuiltins.UUID]


def _is_built_in_conversion(type_val: TypeVal) -> bool:
    """Check if a type is one of our built-in conversions."""
    return type_val in _BUILT_IN_CONVERSIONS or (
        isinstance(type_val, typesys.Instantiation) and type_val.instantiates in _BUILT_IN_CONVERSIONS
    )


def _get_built_in_conversion_name(type_val: TypeVal) -> str:
    """Return the name of a built-in conversion."""
    assert _is_built_in_conversion(type_val)

    if isinstance(type_val, typesys.TypeDef):
        return type_val.name

    assert isinstance(type_val, typesys.Instantiation)
    assert isinstance(type_val.instantiates, typesys.GenericTypeDef)
    return type_val.instantiates.name


class NanobindDependencies:  # noqa: PLW1641 Intentionally leaving out __hash__ because this is a mutable type.
    """Various dependencies a nanobind target can have."""

    def __init__(
        self,
        python_targets: set[nanobinding_registry.TargetId] | None = None,
        cpp_includes: set[Header | SystemHeader] | None = None,
    ) -> None:
        """Initialize a new dependency set."""
        self._python_targets = python_targets or set()
        self._cpp_includes = cpp_includes or set()

    @override
    def __eq__(self, other: object) -> bool:
        """Equality comparison."""
        if isinstance(other, NanobindDependencies):
            return self._python_targets == other._python_targets and self._cpp_includes == other._cpp_includes
        return False

    def update(self, other: NanobindDependencies) -> None:
        """Merge dependency sets."""
        self._python_targets.update(other.get_python_targets())
        self._cpp_includes.update(other.get_cpp_includes())

    def get_python_targets(self) -> list[nanobinding_registry.TargetId]:
        """Get the (sorted) list of python module target."""
        return sorted(self._python_targets)

    def get_cpp_includes(self) -> list[Header | SystemHeader]:
        """Get the (sorted) list of C++ includes."""
        return sorted(self._cpp_includes)


def _get_type_dependent_targets(  # noqa: PLR0911 (One return for each type)
    type_val: TypeVal,
    this_target: TargetId,
    ir_node: ResolvedNanobindBinding,
) -> NanobindDependencies:
    """Lookup first-order nanobind target dependencies in a TypeVal."""
    if _is_bytes_array(type_val):
        return NanobindDependencies(cpp_includes={Header(JEWELS_REPO, path="jewels/nanobind/nb_byte_array.hh")})
    if _is_var_array_type(type_val):
        return _get_type_dependent_targets(_get_var_array_element_type(type_val), this_target, ir_node)
    if _is_fixed_array_type(type_val):
        return _get_type_dependent_targets(_get_fixed_array_element_type(type_val), this_target, ir_node)
    if _is_optional_type(type_val):
        return _get_type_dependent_targets(_get_optional_element_type(type_val), this_target, ir_node)
    if isinstance(type_val, typesys.Instantiation) and type_val.instantiates == clkbuiltins.UUID:
        return NanobindDependencies(cpp_includes={Header(JEWELS_REPO, path="jewels/nanobind/uuid_caster.hh")})
    if type_val in [clkbuiltins.SYNC_TIME, clkbuiltins.DURATION]:
        return NanobindDependencies(
            python_targets={
                nanobinding_registry.TargetId(
                    name="nb_sync_time", module_id=ModuleID(JEWELS_REPO, "jewels::nanobind::nb_sync_time")
                )
            },
        )
    if (
        _is_var_string_type(type_val)
        or isinstance(type_val, PrimitiveType | StrongType)
        or _is_built_in_conversion(type_val)
    ):
        return NanobindDependencies()

    maybe_target_info = nanobinding_registry.lookup_binding(type_val, ir_node.module.context)
    if maybe_target_info is None:
        # If there isn't target info, then either
        # 1. A user-defined schema isn't bound in any nanobind_target.
        # 2. A type isn't in _BUILT_IN_CONVERSIONS because we don't have a type_caster for it.
        pretty = _pretty_binding_name(ir_node)
        pretty_subtype = _sanitized_element_type_name(type_val, ir_node.module.context)
        msg = f"While parsing '{pretty}', found child type '{pretty_subtype}' that doesn't have a nanobind target.\n"
        msg += "There are two likely causes:\n"
        msg += "1. If this is a user-defined type then you must bind it in a nanobind_target.\n"
        msg += "2. If this is a built-in type then it is not yet supported.\n"
        msg += "   A nanobind::type_caster should be implemented (and #included by the clockwork nanobind generator).\n"
        msg += "   Then the type should be added to clockwork.dsl.ir.nanobind_registry._BUILT_IN_CONVERSIONS."
        msg = ir_node.append_error_line(msg)
        raise ValueError(msg)

    if maybe_target_info.target_id == this_target:  # target doesn't depend on itself
        return NanobindDependencies()
    return NanobindDependencies(python_targets={maybe_target_info.target_id})


def _pretty_binding_name(binding: ResolvedNanobindBinding) -> str:
    """Make a human-readable type for this binding of the form '{module}::{name}'."""
    if isinstance(binding.original_type, typesys.Instantiation):
        schema_ir: schema.InstantiatedSchema = to_schema_instantiation(binding.original_type)
        return f"{schema_ir.schema.inner_scope.uniq_path}::{schema_ir.schema_name}"
    if isinstance(binding.original_type, clkenum.ResolvedEnum):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        return f"{binding.original_type.inner_scope}::{binding.original_type.name}"
    # sanity check
    msg = f"error: internal error: unhandled type {binding.original_type}"
    raise TypeError(msg)


def get_dependent_targets(bindings: list[ResolvedNanobindBinding], this_target: TargetId) -> NanobindDependencies:
    """Get first-order dependencies on other nanobind targets.

    Look at all fields of every schema bound in this target, and find their bindings.
    Recursively traverse into VarArrays, FixedArrays, and Optional types.
    """
    dependencies = NanobindDependencies()
    for binding in bindings:
        if isinstance(binding.original_type, typesys.Instantiation):
            schema_ir: schema.InstantiatedSchema = to_schema_instantiation(binding.original_type)
            for field in schema_ir.fields.values():
                field_type = field.type_info
                dependencies.update(_get_type_dependent_targets(field_type, this_target, binding))
        elif isinstance(binding.original_type, clkenum.ResolvedEnum):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            # enums don't have dependencies
            continue
        else:
            # sanity check
            msg = f"error: unhandled type {binding.original_type}"
            raise TypeError(msg)
    return dependencies


def _is_optional_element_passed_as_pointer(optional_element_type: TypeVal) -> bool:
    """Optional is passed by pointer for non-primitive, and pointer for primitive types.

    In python we want the type to be "T | None", and we achieve this with nanobind's std::optional type caster.
    Because we need to return by reference, and std::optional<T> copies, we need to return optional<T*>.

    Nanobind doesn't support returning primitives by reference (presumably because python wraps them anyways, so a
    primitive "value" has the same mutability semantics as a non-primitive "reference").

    This function decides whether we are going to pass the contents of optional<> by reference or value.
    """
    if (
        _is_var_string_type(optional_element_type)
        or isinstance(optional_element_type, PrimitiveType | StrongType | clkenum.ResolvedEnum)
        or _is_built_in_conversion(optional_element_type)
    ):
        return False

    if _is_fixed_array_type(optional_element_type) or _is_var_array_type(optional_element_type):
        return True

    # sanity check that it's a schema
    if not isinstance(optional_element_type, schema.InstantiatedSchema):
        msg = f"Optional element type is not InstantiatedSchema: {optional_element_type}"
        raise TypeError(msg)

    return True


def _make_raw_string(doc: str) -> str:
    """Make a C++ raw string.

    Make sure the delimiter doesn't exist in the given string, and change the delimiter if necessary.
    This function exists because of this documentation found in the vehicle interface debug message:
    // "1" if "hasChanged(counter)&&(counter == uint8(prev_counter+uint8(1))" otherwise "0"

    Fail if we can't sanitize the string.
    """
    # if the delimiter shows up, try increasingly unlikely delimiters
    for delimiter in ["", *[delim * k for k in range(1, 16) for delim in ["|", "_", "#"]]]:
        if f'){delimiter}"' not in doc:
            return f'R"{delimiter}({doc}){delimiter}"'

    # if we got super duper unlucky and can't find one
    err = f"Unable to sanitize documentation string for nanobind:\n{doc}"
    raise ValueError(err)


def _render_optional_field_prop_rw(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef, tappy_cpp_type: str
) -> str:
    """Render a nb::def_prop_rw field property for an Optional field type."""
    optional_element_type = _get_optional_element_type(field.type_info)
    optional_contained_type = _to_cpp_type(compiler_context, optional_element_type).render("")

    if _is_optional_element_passed_as_pointer(optional_element_type):
        maybe_take_address = "&"
        maybe_dereference = "*"
        maybe_pointer = "*"
    else:
        maybe_take_address = ""
        maybe_dereference = ""
        maybe_pointer = ""

    arg_type = f"std::optional<{optional_contained_type}{maybe_pointer}>"
    return f"""\
    .def_prop_rw("{field.cur_name}",
      // getter
      []({tappy_cpp_type}& value) -> {arg_type}
      {{
        return value.get_underlying_{field.cur_name}().has_value()
            ? std::make_optional({maybe_take_address}value.get_underlying_{field.cur_name}().value())
            : std::nullopt;
      }},
      // setter
      []({tappy_cpp_type} &value, const {arg_type} field_value) -> void
      {{
        if (field_value.has_value()) {{
          value.set_{field.cur_name}({maybe_dereference}field_value.value());
        }} else {{
          value.reset_{field.cur_name}();
        }}
      }},
      {_make_raw_string(field.doc.value)},
      nb::for_setter(nb::arg().none())
    )\
"""


# We have to suppress PLR0913 (too many args) because this is already an extremely simple function that can't be split but still needs all these args. The args are all different types so mypy will catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
def _render_field_prop_rw(  # noqa: PLR0913 (See above)
    *,
    compiler_context: CompilerContext,
    field: schema.InstantiatedFieldDef,
    tappy_cpp_type: str,
    render_getter: Callable[[str], str],
    render_setter: Callable[[str, str], str],
    extras: list[str],
) -> str:
    """Render a nb::def_prop_rw field property for a generic field type.

    Different field types are set differently, so getter/setter renderers are passed in as arguments.
    """
    field_cpp_type = _to_cpp_type(compiler_context, field.type_info)
    rendered_field_type = field_cpp_type.render("")

    rendered_extras = "".join([",\n      " + extra for extra in extras])
    return f"""\
    .def_prop_rw("{field.cur_name}",
      // getter
      []({tappy_cpp_type}& value) -> {rendered_field_type}&
      {{
        return {render_getter("value")};
      }},
      // setter
      []({tappy_cpp_type} &value, const {rendered_field_type}& field_value) -> void
      {{
          {render_setter("value", "field_value")};
      }},
      {_make_raw_string(field.doc.value)}{rendered_extras}
    )\
"""


def _render_normal_field_prop_rw(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef, tappy_cpp_type: str
) -> str:
    """Render a nb::def_prop_rw field property for a normal (non-container) field type.

    This includes primitive types, strong types, enums, and other schemas.
    """

    def render_getter(value: str) -> str:
        return f"{value}.get_mutable_{field.cur_name}()"

    def render_setter(value: str, field_value: str) -> str:
        return f"{value}.set_{field.cur_name}({field_value})"

    return _render_field_prop_rw(
        compiler_context=compiler_context,
        field=field,
        tappy_cpp_type=tappy_cpp_type,
        render_getter=render_getter,
        render_setter=render_setter,
        extras=[],
    )


def _python_array_or_list_type_expr(
    compiler_context: CompilerContext, type_val: typesys.TypeVal, *, use_union_type: bool
) -> str:
    """Get a C++ expression that will render the python type of an array.

    This will return an expression something like 'python_type_name<VarArray<Msg, 22>>() + | list[" + python_type_name<Msg>() + "]"'.
    It will evaluate to something like 'VarArray_Msg_22 | list[Msg]'.
    """
    if _is_fixed_array_type(type_val):
        elem_type = _get_fixed_array_element_type(type_val)
    elif _is_var_array_type(type_val):
        elem_type = _get_var_array_element_type(type_val)
    else:
        msg = f"Can't get array type expression: type is not VarArray or FixedArray: {type_val}"
        raise ValueError(msg)

    cpp_type = _to_cpp_type(compiler_context, type_val).render("")
    py_cpp_type = f"::jewels::nanobind::python_type_name<{cpp_type}>()"
    if use_union_type:
        elem_cpp_type = _to_cpp_type(compiler_context, elem_type).render("")
        py_cpp_element_type = f"::jewels::nanobind::python_type_name<{elem_cpp_type}>()"
        return f'{py_cpp_type} + " | list[" + {py_cpp_element_type} + "]"'
    return py_cpp_type


def _get_python_type_expr(compiler_context: CompilerContext, type_val: typesys.TypeVal, *, use_union_type: bool) -> str:
    """Get a C++ expression that will render the python type.

    This will return an expression something like 'python_type_name<VarArray<Msg, 22>>() + | list[" + python_type_name<Msg>() + "]"'.
    It will evaluate to something like 'VarArray_Msg_22 | list[Msg]'.
    """
    if _is_optional_type(type_val):
        optional_element_type = _get_optional_element_type(type_val)
        return (
            _get_python_type_expr(compiler_context, optional_element_type, use_union_type=use_union_type)
            + ' + " | None"'
        )
    if _is_var_array_type(type_val) or _is_fixed_array_type(type_val):
        return _python_array_or_list_type_expr(compiler_context, type_val, use_union_type=use_union_type)
    cpp_type = _to_cpp_type(compiler_context, type_val).render("")
    return f"::jewels::nanobind::python_type_name<{cpp_type}>()"


def _render_var_string_field_prop_rw(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef, tappy_cpp_type: str
) -> str:
    """Render a nb::def_prop_rw field property for a VarString field type."""

    def render_getter(value: str) -> str:
        return f"{value}.get_underlying_{field.cur_name}()"

    def render_setter(value: str, field_value: str) -> str:
        return f"{value}.get_underlying_{field.cur_name}() = {field_value}"

    return _render_field_prop_rw(
        compiler_context=compiler_context,
        field=field,
        tappy_cpp_type=tappy_cpp_type,
        render_getter=render_getter,
        render_setter=render_setter,
        extras=[],
    )


def _render_var_array_field_prop_rw(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef, tappy_cpp_type: str
) -> str:
    """Render a nb::def_prop_rw field property for a VarArray field type."""

    def render_getter(value: str) -> str:
        return f"{value}.get_underlying_{field.cur_name}()"

    def render_setter(value: str, field_value: str) -> str:
        return f"{value}.get_underlying_{field.cur_name}() = {field_value}"

    if _is_bytes_array(field.type_info):
        # Byte arrays use a caster type.
        extras = [
            f'nb::for_getter(nb::sig("def {field.cur_name}(self) -> bytes"))',
            f'nb::for_setter(nb::sig("def {field.cur_name}(self, arg: bytes, /) -> None"))',
        ]
    else:
        # for both setter & getter
        py_type_expr_property = _get_python_type_expr(compiler_context, field.type_info, use_union_type=False)
        extras = [
            f'nb::for_getter(nb::sig(std::string("def {field.cur_name}(self) -> " + {py_type_expr_property}).c_str()))',
            f'nb::for_setter(nb::sig(std::string("def {field.cur_name}(self, arg: " + {py_type_expr_property} + ", /) -> None").c_str()))',
        ]

    return _render_field_prop_rw(
        compiler_context=compiler_context,
        field=field,
        tappy_cpp_type=tappy_cpp_type,
        render_getter=render_getter,
        render_setter=render_setter,
        extras=extras,
    )


def _render_fixed_array_field_prop_rw(
    compiler_context: CompilerContext, field: schema.InstantiatedFieldDef, tappy_cpp_type: str
) -> str:
    """Render a nb::def_prop_rw field property for a FixedArray field type."""
    field_cpp_type = _to_cpp_type(compiler_context, field.type_info)
    rendered_field_type = field_cpp_type.render("")

    def render_getter(value: str) -> str:
        # We want nanobind to manage a reference to the std::array field in the Tachyon representation.
        # Unfortunately, Tap doesn't expose it with a 'get_underlying_' accessor. There is only a `get_mutable_` accessor
        # which exposes a gsl::span. So we have to reinterpret_cast the gsl::span data back to the true std::array<>.
        return f"*reinterpret_cast<{rendered_field_type}*>({value}.get_mutable_{field.cur_name}().data())"

    def render_setter(value: str, field_value: str) -> str:
        return f"{value}.set_{field.cur_name}({field_value})"

    if _is_bytes_array(field.type_info):
        # Byte arrays use a caster type.
        extras = [
            f'nb::for_getter(nb::sig("def {field.cur_name}(self) -> bytes"))',
            f'nb::for_setter(nb::sig("def {field.cur_name}(self, arg: bytes, /) -> None"))',
        ]
    else:
        # for both setter & getter
        py_type_expr_property = _get_python_type_expr(compiler_context, field.type_info, use_union_type=False)
        extras = [
            f'nb::for_getter(nb::sig(std::string("def {field.cur_name}(self) -> " + {py_type_expr_property}).c_str()))',
            f'nb::for_setter(nb::sig(std::string("def {field.cur_name}(self, arg: " + {py_type_expr_property} + ", /) -> None").c_str()))',
        ]

    return _render_field_prop_rw(
        compiler_context=compiler_context,
        field=field,
        tappy_cpp_type=tappy_cpp_type,
        render_getter=render_getter,
        render_setter=render_setter,
        extras=extras,
    )


def _render_constructor(
    compiler_context: CompilerContext, fields: list[schema.InstantiatedFieldDef], tappy_cpp_type: str
) -> str:
    """Render a constructor binding that takes all fields as arguments.

    Uses source code order, since that's the only type of non-default constructor clockwork has right now.
    """
    args_list = []  # int32& field1, Foo& field2, ...
    extras_list = []  # nb::arg("foo"), nb::arg("bar").none(), ...
    init_exprs_list = []  # ["field1", "field2.has_value() ? field2.value() : std::nullopt"]

    set_optionals_list = []
    for k, field in enumerate(fields):
        param_name = f"arg{k}__{field.cur_name}"

        if not _is_optional_type(field.type_info):
            cpp_type = _to_cpp_type(compiler_context, field.type_info).render("") + "&"
            init_expr = param_name
            extra = f'nb::arg("{field.cur_name}")'
        else:
            elem_type = _get_optional_element_type(field.type_info)
            elem_cpp_type = _to_cpp_type(compiler_context, elem_type).render("")
            extra = f'nb::arg("{field.cur_name}").none()'
            maybe_ptr = "*" if _is_optional_element_passed_as_pointer(elem_type) else ""
            # Always set Optionals to nullopt, maybe we'll set them afterwards.
            # This is to avoid actually forming an Optional<> value, which would copying on the stack.
            init_expr = f"::jewels::tap::Optional<{elem_cpp_type}>()"
            cpp_type = f"std::optional<{elem_cpp_type}{maybe_ptr}>"

            # Afterwards go back and set the optional fields. This way we avoid copying on the stack.
            set_optional_expr = f"""\
             if ({param_name}.has_value())
             {{
               self->set_{field.cur_name}({maybe_ptr}{param_name}.value());
             }}
"""
            set_optionals_list.append(set_optional_expr)

        args_list.append(f"const {cpp_type} {param_name}")
        extras_list.append(extra)
        init_exprs_list.append(init_expr)

    # customize the signature to allow list types
    signature_args = ", ".join(
        [
            f'{field.cur_name}: " + {_get_python_type_expr(compiler_context, field.type_info, use_union_type=True)} + "'
            for field in fields
        ]
    )
    extras_list.append(f'nb::sig(("def __init__(self, {signature_args}) -> None").c_str())')

    args = ",\n         ".join(args_list)
    designated_initializers = "\n".join(
        [
            f"               .{field.cur_name} = {init_expr},"
            for field, init_expr in zip(fields, init_exprs_list, strict=True)
        ]
    )
    extras = ",\n      ".join(extras_list)

    set_optionals = "\n".join(set_optionals_list)

    tap_init_type = tappy_cpp_type.replace(
        "clockwork::Tap<::clockwork::Tachyon<", "clockwork::TapInit<::clockwork::Tachyon<"
    )

    return f"""\
    // source-code order constructor
    .def(
      "__init__",
      []({tappy_cpp_type}* self,
         {args})
           {{
             new (self) {tappy_cpp_type}({tap_init_type}{{
{designated_initializers}
             }});

{set_optionals}
           }},
      {extras}
    )\
"""


def _def_generic_eq_and_ne(rendered_cpp_type: str) -> str:
    return f"""\
    .def("__eq__", [](const {rendered_cpp_type}& lhs, nb::handle rhs_handle) -> bool {{
        return ::jewels::nanobind::casted_value_is_equal(lhs, rhs_handle);
      }})
    .def("__ne__", [](const {rendered_cpp_type}& lhs, nb::handle rhs_handle) -> bool {{
        return !(::jewels::nanobind::casted_value_is_equal(lhs, rhs_handle));
      }})"""


def sanitize_uniqpath(uniq_path: str) -> str:
    """External modules have an @ prefix."""
    if uniq_path.startswith("@"):
        return uniq_path[1:]
    return uniq_path


def _sanitized_element_type_name(type_val: TypeVal, compiler_context: CompilerContext) -> str:
    """Form a sanitized name for a non-container type.

    Used for naming container bindings - for example this would be the Foo in "VarArray_Foo_22" or the Int32 in "FixedArray_Int32_20".

    Can't use the python type name because multiple C++ types map to the same python type,
    for instance all C++ become the same python int, and aurora units have the same python type as their underlying representation.
    """
    if isinstance(type_val, clkenum.ResolvedEnum):
        assert type_val.inner_scope.uniq_path.endswith(type_val.name)
        return sanitize_uniqpath(type_val.inner_scope.uniq_path).replace("::", "_")

    if isinstance(type_val, schema.InstantiatedSchema):
        assert type_val.schema.inner_scope.uniq_path.endswith(type_val.schema.name)

        # use the generic alias if there is one
        target_info = nanobinding_registry.lookup_binding(type_val, compiler_context)
        if target_info and target_info.maybe_generic_alias is not None:
            return target_info.maybe_generic_alias

        return sanitize_uniqpath(type_val.schema.inner_scope.uniq_path).replace("::", "_")

    if isinstance(type_val, PrimitiveType | StrongType):
        return type_val.name

    if _is_var_string_type(type_val):
        max_size = _get_var_string_max_size(type_val)
        return f"VarString_{max_size}"

    if _is_built_in_conversion(type_val):
        return _get_built_in_conversion_name(type_val)

    msg = f"Couldn't form a sanitized and unique name for {type_val}"
    raise TypeError(msg)


def _render_bind_vectors_and_arrays(
    compiler_context: CompilerContext, cpp_mod: context.CppChunk, type_val: TypeVal
) -> str:
    """Write the nb::bind_vector for embedded VarArrays and the jewels::nanobind::bind_array for embedded FixedArrays.

    Recursively check element types of Optional, FixedArray, and VarArray, and bind any internal VarArray/FixedArray types as well.

    Return a C++ expression which would evaluate to the *python* type.
    For instance, for optional<Foo> it would be 'jewels::nanobind::python_type_name<Foo>() + " | None"'.
    """
    if _is_optional_type(type_val):
        inner_expr = _render_bind_vectors_and_arrays(compiler_context, cpp_mod, _get_optional_element_type(type_val))
        return f"Optional_{inner_expr}"

    # check if it's a fixed or var array
    if _is_bytes_array(type_val):
        return "bytes"
    if _is_fixed_array_type(type_val):
        array_is_fixed_length = True
        element_type = _get_fixed_array_element_type(type_val)
        cpp_mod.context.add_include(Header(JEWELS_REPO, "jewels/nanobind/clk_bindings/bind_fixed_array.hh"))
    elif _is_var_array_type(type_val):
        array_is_fixed_length = False
        element_type = _get_var_array_element_type(type_val)
        cpp_mod.context.add_include(Header(JEWELS_REPO, "jewels/nanobind/clk_bindings/bind_var_array.hh"))
    else:
        # not an array or optional type - nothing to bind here, just return a unique sanitized type name
        return _sanitized_element_type_name(type_val, compiler_context)

    # Recursively bind any vector or array types in the element type.
    # For example, if this is vector<vector<optional<array<T> > > >, we want to bind them all from the innermost on out.
    inner_expr = _render_bind_vectors_and_arrays(compiler_context, cpp_mod, element_type)

    # The return-value policy 'reference_internal' is the whole point of doing these bindings.
    # This is what gives us the same mutability and ownership semantics as native python dataclasses.
    rv_policy = "nb::rv_policy::reference_internal"

    rendered_element_cpp_type = _to_cpp_type(compiler_context, element_type).render("")
    rendered_cpp_type = _to_cpp_type(compiler_context, type_val).render("")
    if array_is_fixed_length:
        size = _get_fixed_array_size(type_val)
        py_type_name = f"FixedArray_{inner_expr}_{size}"
        cpp_mod.append(
            f"  jewels::nanobind::bind_array<{rendered_cpp_type}, {rendered_element_cpp_type}, {rv_policy}>(py_module,"
        )
        cpp_mod.append(f'      "{py_type_name}");')
    else:
        max_size = _get_var_array_max_size(type_val)
        py_type_name = f"VarArray_{inner_expr}_{max_size}"
        cpp_mod.append(
            f"  jewels::nanobind::bind_var_array<{rendered_cpp_type}, {rendered_element_cpp_type}, {rv_policy}>(py_module,"
        )
        cpp_mod.append(f'      "{py_type_name}");')

    return py_type_name


@dataclass
class SchemaInfo:
    """Small wrapper around the schema."""

    def __init__(
        self,
        compiler_context: CompilerContext,
        binding_original_type: typesys.Instantiation,
        binding_alias_name: str | None,
    ) -> None:
        """Wrap a schema instantiation."""
        self._compiler_context = compiler_context
        self._binding_original_type = binding_original_type
        self._schema_ir = to_schema_instantiation(binding_original_type)
        self._class_name = binding_alias_name or self._schema_ir.schema_name
        self._class_binding_variable = f"class_def_{self._class_name}"
        self._tappy_cpp_type = _to_cpp_type(self._compiler_context, self._schema_ir)
        self._rendered_tappy_cpp_type = self._tappy_cpp_type.render("")

    def render_declaration(self) -> str:
        """Render the initialization of the nb::class_ object."""
        return f'  nb::class_<{self._rendered_tappy_cpp_type}> {self._class_binding_variable}(py_module, "{self._class_name}");'

    def render_binding(self) -> context.CppChunk:
        """Render the caster into a cpp module."""
        schema_ir: schema.InstantiatedSchema = self._schema_ir

        cpp_mod = context.CppChunk()

        cpp_mod.context.add_includes(self._tappy_cpp_type.includes)

        # nb::bind_vector<> for all VarArrays, nb::bind_array<> for all FixedArrays
        for field in schema_ir.fields.values():
            _render_bind_vectors_and_arrays(self._compiler_context, cpp_mod, field.type_info)

        cpp_mod.append(f"""\
  {self._class_binding_variable}
    // Default constructor.
    .def(nb::init<>(), "Default constructor.")
    // PyTachyon2-compatible default constructor.
    .def_static("default", [](){{return {self._rendered_tappy_cpp_type}();}})
    // Copy constructor.
    .def(nb::init<{self._rendered_tappy_cpp_type}>(), "Copy constructor.")\
    // PyTachyon2-compatible copy and deepcopy.
    .def("__copy__", [](const {self._rendered_tappy_cpp_type}& value) -> std::unique_ptr<{self._rendered_tappy_cpp_type}> {{
        return std::make_unique<{self._rendered_tappy_cpp_type}>(value);
        }})
    .def("__deepcopy__", [](const {self._rendered_tappy_cpp_type}& value, nb::handle /* memo */) -> std::unique_ptr<{self._rendered_tappy_cpp_type}> {{
        return std::make_unique<{self._rendered_tappy_cpp_type}>(value);
        }})\
""")

        # Make a constructor taking all fields if the schema enables that option.
        # Currently assumes source_code_order because that's the only option currently implemented in the clockwork language.
        if (
            schema_ir.schema.options is not None
            and schema_ir.schema.options.provide_constructor is True
            and schema_ir.fields.values()
        ):
            cpp_mod.append(
                _render_constructor(
                    self._compiler_context, list(schema_ir.fields.values()), self._rendered_tappy_cpp_type
                )
            )

        # render field property bindings
        for field in schema_ir.fields.values():
            field_type = field.type_info
            field_cpp_type = typereg.get_cpp_type(self._compiler_context, field_type)
            assert isinstance(field_cpp_type, types.CppType | types.CppTemplateType)

            if _is_optional_type(field_type):
                cpp_mod.append(
                    _render_optional_field_prop_rw(self._compiler_context, field, self._rendered_tappy_cpp_type)
                )
            elif _is_var_array_type(field_type):
                cpp_mod.append(
                    _render_var_array_field_prop_rw(self._compiler_context, field, self._rendered_tappy_cpp_type)
                )
            elif _is_var_string_type(field_type):
                cpp_mod.append(
                    _render_var_string_field_prop_rw(self._compiler_context, field, self._rendered_tappy_cpp_type)
                )
            elif _is_fixed_array_type(field_type):
                cpp_mod.append(
                    _render_fixed_array_field_prop_rw(self._compiler_context, field, self._rendered_tappy_cpp_type)
                )
            else:
                cpp_mod.append(
                    _render_normal_field_prop_rw(self._compiler_context, field, self._rendered_tappy_cpp_type)
                )

        cpp_mod.append(self._def_str_or_repr("str"))
        cpp_mod.append(self._def_str_or_repr("repr"))
        cpp_mod.append(_def_generic_eq_and_ne(self._rendered_tappy_cpp_type) + ";")

        # tachyon serialize
        cpp_mod.append("")
        cpp_mod.append("    // tachyon serialize")
        cpp_mod.append(f"    jewels::nanobind::bind_tachyon_serialize({self._class_binding_variable});")
        self._render_bind_tachyon_constraint_and_metadata(cpp_mod)

        return cpp_mod

    def _def_str_or_repr(self, str_or_repr: Literal["str", "repr"]) -> context.CppChunk:
        """Define __str__ or __repr__ by recursively iterating over fields and calling str/repr on them."""
        schema_ir: schema.InstantiatedSchema = self._schema_ir
        cpp_mod = context.CppChunk()
        cpp_mod.context.add_include(SystemHeader("sstream"))
        cpp_mod.append(f"""\
    .def("__{str_or_repr}__", [](nb::object self) -> nb::str {{
      std::ostringstream oss;
      oss << "{schema_ir.schema_name}(";\
""")
        for k, field in enumerate(schema_ir.fields.values()):
            comma_or_close_paren = ", " if k < len(schema_ir.fields) - 1 else ")"
            field_var = f"field_{field.cur_name}"
            cpp_mod.append(f"""\
      nb::object {field_var} = self.attr("{field.cur_name}");
      oss << "{field.cur_name}=" << nb::{str_or_repr}({field_var}).c_str() << "{comma_or_close_paren}";
""")
        cpp_mod.append("""\
      return nb::str(oss.str().c_str());
    })""")
        return cpp_mod

    def _render_bind_tachyon_constraint_and_metadata(self, cpp_mod: context.CppChunk) -> None:
        """Render 'bind_tachyon_constraint_and_metadata'."""
        # get the metadata and constraint
        constraint = tachyon_reg.constraint_for_type(self._compiler_context, self._schema_ir)
        if constraint is None:
            err = f"Error looking up tachyon constraint in the tachyon registry for {self._schema_ir.schema.inner_scope.uniq_path}"
            raise ValueError(err)

        metadata_name = self._schema_ir.value_key()

        # render a call to the C++ function 'bind_tachyon_constraint_and_metadata', which binds the methods
        cpp_mod.append(f"""\
    jewels::nanobind::bind_tachyon_constraint_and_metadata(
        {self._class_binding_variable},
        "{metadata_name}",
        {constraint.size},
        {constraint.alignment},
        "{self._schema_ir.schema.module.module_id.repo}",
        "{self._schema_ir.schema.module.module_id.get_base_path()}",
        "{self._schema_ir.schema_name}");""")


def _render_enum_binding(resolved_enum: clkenum.ResolvedEnum) -> context.CppChunk:
    """Render a nb::enum_<> binding."""
    cpp_mod = context.CppChunk()

    enum_type = typereg.get_cpp_type(resolved_enum.module.context, resolved_enum)
    assert isinstance(enum_type, types.CppType)

    cpp_mod.context.add_includes(enum_type.includes)
    enum_cpp_type = enum_type.render("")

    enum_params = ["py_module", f'"{resolved_enum.name}"', _make_raw_string(resolved_enum.doc.value)]
    if resolved_enum.bit_flags:
        enum_params += ["nb::is_flag()"]
    cpp_mod.append(f"  nb::enum_<{enum_cpp_type}>({', '.join(enum_params)})")
    for value in resolved_enum.values.values():
        cpp_mod.append(
            f'    .value("{value.name}", {enum_cpp_type}::{value.name}, {_make_raw_string(value.doc.value)})'
        )
    cpp_mod.append(";")

    return cpp_mod


def _render_decimal_literal_constant(
    binding_value: primitive.DecimalLiteral, binding_typespec: TypeExpression | None, retain_float32: bool
) -> str:
    """Render a DecimalLiteral constant as a nanobind expression (like 'nb::float_(2.2)')."""
    if binding_typespec is None:
        # no type signature - just look for a decimal in the value
        if "." in str(binding_value.value):
            return f"nb::float_({binding_value.value})"
        return f"nb::int_({binding_value.value})"

    type_val = binding_typespec.resolved_value
    assert type_val is not None, f"Expected a resolved type for {binding_typespec}"
    is_strong_type = isinstance(type_val, StrongType)
    if type_val == clkbuiltins.FLOAT32 or (is_strong_type and type_val.typespec == clkbuiltins.FLOAT32):
        # make sure 32 bit floats reach python with correct precision
        maybe_f = "f" if retain_float32 else ""
        return f"nb::float_({binding_value.value}{maybe_f})"
    if type_val == clkbuiltins.FLOAT64 or (is_strong_type and type_val.typespec == clkbuiltins.FLOAT64):
        return f"nb::float_({binding_value.value})"
    if isinstance(type_val, clkbuiltins.IntegerPrimitiveType) or (
        is_strong_type and isinstance(type_val.typespec, clkbuiltins.IntegerPrimitiveType)
    ):
        return f"nb::int_({binding_value.value})"
    msg = f"Unsupported type for decimal literal: {type_val}"
    raise TypeError(msg)


class Constant:
    """Wrapper around ImmutableBinding, for rendering constants."""

    # 32 bit floats can be exposed exactly, but we prefer to cast to python `float` the
    # same way way we convert all int types to python `int`.
    _RETAIN_FLOAT32 = False

    def __init__(self, binding: ImmutableBinding) -> None:
        """Render a constant as a nanobind binding assignment."""
        self._binding = binding
        if binding.value is clkbuiltins.FALSE_VALUE:
            self._nb_value = "nb::bool_(false)"
        elif binding.value is clkbuiltins.TRUE_VALUE:
            self._nb_value = "nb::bool_(true)"
        elif isinstance(binding.value, primitive.StringLiteral):
            self._nb_value = f'nb::str("{binding.value.value}")'
        elif isinstance(binding.value, primitive.DecimalLiteral):
            self._nb_value = _render_decimal_literal_constant(binding.value, binding.typespec, self._RETAIN_FLOAT32)
        else:
            msg = binding.append_error_line(
                f"Converting constant type '{type(binding.value)}' to a nanobind value is unsupported."
            )
            raise TypeError(msg)

    def render_constant(self) -> str:
        """Render the binding static definition."""
        name = self._binding.name.upper()
        return f'py_module.attr("{name}") = {self._nb_value};'


def _extract_enums_and_schemas(
    compiler_context: CompilerContext,
    bindings: list[ResolvedNanobindBinding],
) -> tuple[list[clkenum.ResolvedEnum], list[SchemaInfo]]:
    """Extract enums and schemas from bindings.

    Raises TypeError if any binding is not recognized as an enum or schema.
    """
    enums: list[clkenum.ResolvedEnum] = []
    schemas: list[SchemaInfo] = []
    for binding in bindings:
        if isinstance(binding.original_type, clkenum.ResolvedEnum):
            enums.append(binding.original_type)
        elif isinstance(binding.original_type, typesys.Instantiation):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            schemas.append(SchemaInfo(compiler_context, binding.original_type, binding.alias_name))
        else:
            msg = binding.append_error_line(f"error: unhandled type {binding.original_type}")
            raise TypeError(msg)

    return enums, schemas


def render_nanobind_bindings_module(
    compiler_context: CompilerContext,
    bindings: list[ResolvedNanobindBinding],
    constant_bindings: list[ImmutableBinding],
    target_name: str,
    dependent_targets: NanobindDependencies,
) -> context.CppChunk:
    """Generate C++ module chunks for a nanobind bindings module."""
    cpp_mod = context.CppChunk()

    # set up includes
    cpp_mod.context.add_includes(
        [
            Header(CLK_REPO, "clockwork/repr_iface.hh"),
            Header(JEWELS_REPO, "jewels/nanobind/clk_bindings/bind_tachyon_serialize.hh"),
            # All type casters are always brought into scope, to rule out any potential interaction corner cases.
            Header(JEWELS_REPO, "jewels/nanobind/nb_var_string.hh", iwyu_pragma="IWYU pragma: keep"),
            Header(JEWELS_REPO, "jewels/nanobind/nb_au.hh", iwyu_pragma="IWYU pragma: keep"),
            Header(JEWELS_REPO, "jewels/nanobind/clk_bindings/get_python_type_name.hh"),
            Header(JEWELS_REPO, "jewels/nanobind/clk_bindings/cast_maybe_by_reference.hh"),
            SystemHeader("nanobind/nanobind.h"),
            SystemHeader("nanobind/stl/optional.h", iwyu_pragma="IWYU pragma: keep"),
            SystemHeader("nanobind/stl/unique_ptr.h", iwyu_pragma="IWYU pragma: keep"),
            SystemHeader("memory"),
            SystemHeader("optional"),
        ]
    )
    cpp_mod.context.add_includes(dependent_targets.get_cpp_includes())

    # open the body
    cpp_mod.append(f"NB_MODULE({target_name}, py_module)")
    cpp_mod.append("{")
    cpp_mod.append("  namespace nb = ::nanobind;")

    # write imports
    if dependent_targets.get_python_targets():
        cpp_mod.append("")
        cpp_mod.append("  // import dependencies")
        for dependent_target in dependent_targets.get_python_targets():
            cpp_mod.append(f'  nb::module_::import_("{dependent_target.to_py_module()}");')

    # write constants
    if constant_bindings:
        cpp_mod.append("")
        cpp_mod.append("  // constants")
        for constant in constant_bindings:
            cpp_mod.append("  " + Constant(constant).render_constant())

    # pick out enums and schemas from bindings
    enums, schemas = _extract_enums_and_schemas(compiler_context, bindings)

    # render the enums
    if enums:
        cpp_mod.append("")
        cpp_mod.append("  // enum bindings")
        for enum in enums:
            cpp_mod.append(_render_enum_binding(enum))

    # render the schemas
    if schemas:
        cpp_mod.append("")
        cpp_mod.append("  // schema declarations")
        for schema_info in schemas:
            # Bind all the schema types first, before binding any of the methods.
            # This ensures that when bind_vector/bind_array are called, all schema types are available.
            cpp_mod.append(schema_info.render_declaration())

        cpp_mod.append("")
        cpp_mod.append("  // schema bindings")
        for schema_info in schemas:
            cpp_mod.append(schema_info.render_binding())

    # close the body and return
    cpp_mod.append("}")
    return cpp_mod
