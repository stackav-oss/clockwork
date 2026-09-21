# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""C++ code generation for Struct-of-Arrays (SoA) types.

This module generates C++ template specializations for FixedSoa<T, N> and VarSoa<T, N>
when a schema T has the `soa_enabled: true` option set.

Key generated components:
- ElementRef/ElementConstRef proxy classes for accessing individual SoA elements
- FixedSoa<T, N> struct with array storage for each field
- VarSoa<T, max_size> struct with array storage and size tracking
- SoaTraits specializations to break CRTP circular dependencies
- Helper methods: view_*, compare_fields, construct/wipe_range, etc.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Final

from clockwork.dsl.cpp import typereg, types
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, SystemHeader, comment_doc_string
from clockwork.dsl.ir import clkbuiltins, typesys
from clockwork.dsl.serialization import tachyon_reg, tap

if TYPE_CHECKING:
    from clockwork.dsl.compiler_context import CompilerContext
    from clockwork.dsl.ir import schema


def should_generate_soa(schema_ir: schema.InstantiatedSchema) -> bool:
    """Check if SoA types should be generated for a schema."""
    if schema_ir.options is None:
        return False
    return schema_ir.options.soa_enabled


@dataclass(slots=True, frozen=True)
class FieldAccessorInfo:
    """Accessor information for a field to generate ElementRef methods."""

    cpp_type: types.CppTypeExpr
    is_fixed_array: bool
    is_var_array: bool
    is_var_string: bool
    is_optional: bool
    element_type: types.CppTypeExpr | None
    array_size: types.CppValue | None


def _get_field_accessor_info(
    compiler_context: CompilerContext,
    field: schema.InstantiatedFieldDef,
) -> FieldAccessorInfo:
    """Extract accessor information for a field to generate ElementRef methods."""
    cpp_type = tap.to_cpp_type(compiler_context, field.type_info)

    is_fixed_array = False
    is_var_array = False
    is_var_string = False
    is_optional = False
    element_type: types.CppTypeExpr | None = None
    array_size: types.CppValue | None = None

    field_type = field.type_info
    if isinstance(field_type, typesys.Instantiation):
        if field_type.instantiates is clkbuiltins.FIXED_ARRAY:
            is_fixed_array = True
            element_type_info = field_type.arguments["type"]
            element_type = typereg.get_cpp_type(compiler_context, element_type_info)
            size_value = field_type.arguments["size"]
            cpp_size = tap.value_to_cpp(size_value)
            if isinstance(cpp_size, types.CppValue):
                array_size = cpp_size
        elif field_type.instantiates is clkbuiltins.VAR_ARRAY:
            is_var_array = True
            element_type_info = field_type.arguments["type"]
            element_type = typereg.get_cpp_type(compiler_context, element_type_info)
        elif field_type.instantiates is clkbuiltins.VAR_STRING:
            is_var_string = True
            element_type = types.CppType([], "char", None)
        elif field_type.instantiates is clkbuiltins.OPTIONAL:
            is_optional = True

    return FieldAccessorInfo(
        cpp_type=cpp_type,
        is_fixed_array=is_fixed_array,
        is_var_array=is_var_array,
        is_var_string=is_var_string,
        is_optional=is_optional,
        element_type=element_type,
        array_size=array_size,
    )


def _create_fixed_array_field_accessors(
    field_info: FieldAccessorInfo,
    field_name: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create accessor methods for FixedArray fields."""
    cpp_type = field_info.cpp_type
    element_type = field_info.element_type
    array_size = field_info.array_size
    assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
    assert isinstance(element_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
    assert isinstance(array_size, types.CppValue)

    const_span = types.SPAN.instantiate([types.const_qualify(element_type, True), array_size])
    mutable_span = types.SPAN.instantiate([element_type, array_size])

    object_policy_accessor = f"::jewels::memory::ObjectPolicy<{cpp_type.render('')}>::get({soa_accessor})"

    methods = []

    # get_{field}() const -> span
    methods.append(tap.create_get_span_method(field_name, const_span, True, object_policy_accessor))

    # get_mutable_{field}() -> span
    methods.append(tap.create_get_span_method(field_name, mutable_span, False, object_policy_accessor))

    # set_{field}(span)
    methods.append(tap.create_set_span_method(field_name, const_span, object_policy_accessor))

    return methods


def _create_var_array_field_accessors(
    field_info: FieldAccessorInfo,
    field_name: str,
    field_doc: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create accessor methods for VarArray fields."""
    cpp_type = field_info.cpp_type
    element_type = field_info.element_type
    assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
    assert isinstance(element_type, (types.CppType, types.CppTemplateType, types.CppScopedType))

    const_span = types.SPAN.instantiate([types.const_qualify(element_type, True)])
    mutable_span = types.SPAN.instantiate([element_type])

    underlying_accessor = f"get_underlying_{field_name}()"

    methods = []

    # get_{field}() const -> span (clear ref-qualifiers since ElementRef is a temporary)
    get_span_method = tap.create_get_span_method(field_name, const_span, True, underlying_accessor)
    get_span_method.trailing_qualifiers = ["const"]  # Keep const, remove &
    methods.append(get_span_method)

    # get_mutable_{field}() -> span (clear ref-qualifiers since ElementRef is a temporary)
    get_mutable_span_method = tap.create_get_span_method(field_name, mutable_span, False, underlying_accessor)
    get_mutable_span_method.trailing_qualifiers = []  # Remove &
    methods.append(get_mutable_span_method)

    # get_underlying_{field}() -> VarArray&
    methods.extend(_create_get_underlying_accessors(cpp_type, field_name, field_doc, soa_accessor))

    # try_set_{field}(span)
    methods.append(tap.create_try_set_span_method(field_name, const_span, underlying_accessor, use_callsig=True))

    return methods


def _create_var_string_field_accessors(
    field_info: FieldAccessorInfo,
    field_name: str,
    field_doc: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create accessor methods for VarString fields."""
    cpp_type = field_info.cpp_type
    assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))

    mutable_span = types.SPAN.instantiate([types.CHAR])
    underlying_accessor = f"get_underlying_{field_name}()"

    methods = []

    # get_{field}() const -> string_view (clear ref-qualifiers since ElementRef is a temporary)
    get_string_view_method = tap.create_get_span_method(field_name, types.STRING_VIEW, True, underlying_accessor)
    get_string_view_method.trailing_qualifiers = ["const"]  # Keep const, remove &
    methods.append(get_string_view_method)

    # get_mutable_{field}() -> span<char> (clear ref-qualifiers since ElementRef is a temporary)
    get_mutable_span_method = tap.create_get_span_method(field_name, mutable_span, False, underlying_accessor)
    get_mutable_span_method.trailing_qualifiers = []  # Remove &
    methods.append(get_mutable_span_method)

    # get_underlying_{field}() -> VarString&
    methods.extend(_create_get_underlying_accessors(cpp_type, field_name, field_doc, soa_accessor))

    # try_set_{field}(string_view)
    methods.append(tap.create_try_set_span_method(field_name, types.STRING_VIEW, underlying_accessor, use_callsig=True))

    return methods


def _create_get_underlying_accessors(
    cpp_type: types.CppTypeExpr,
    field_name: str,
    field_doc: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create get_underlying accessors (const and mutable)."""
    assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
    methods = []

    # const version (with & ref-qualifier to prevent returning references to temporaries)
    const_ref = types.const_qualify(types.ref_qualify(cpp_type, types.Ref.L), True)
    get_underlying_body = CppChunk()
    get_underlying_body.append(f"return ::jewels::memory::ObjectPolicy<{cpp_type.render('')}>::get({soa_accessor});")
    methods.append(
        types.CppMethod(
            name=f"get_underlying_{field_name}",
            doc=field_doc,
            return_type=const_ref,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const", "&"],
            body=get_underlying_body,
            no_discard=True,
        )
    )

    # mutable version (with & ref-qualifier to prevent returning references to temporaries)
    mutable_ref = types.ref_qualify(cpp_type, types.Ref.L)
    get_underlying_mut_body = CppChunk()
    get_underlying_mut_body.append(
        f"return ::jewels::memory::ObjectPolicy<{cpp_type.render('')}>::get({soa_accessor});"
    )
    methods.append(
        types.CppMethod(
            name=f"get_underlying_{field_name}",
            doc=field_doc,
            return_type=mutable_ref,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["&"],
            body=get_underlying_mut_body,
            no_discard=True,
        )
    )

    return methods


def _create_regular_field_accessors(
    cpp_type: types.CppTypeExpr,
    field_name: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for regular fields (primitives, subschemas)."""
    assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))

    const_ref = types.const_qualify(types.ref_qualify(cpp_type, types.Ref.L), True)
    mutable_ref = types.ref_qualify(cpp_type, types.Ref.L)

    # Wrap soa_accessor with ObjectPolicy to access the actual object from AlignedStorage
    object_policy_accessor = f"::jewels::memory::ObjectPolicy<{cpp_type.render('')}>::get({soa_accessor})"

    methods = []

    # get_{field}() const - clear ref-qualifiers since ElementRef is designed to be used as temporary
    get_method = tap.create_get_method(field_name, const_ref, "get", object_policy_accessor)
    get_method.trailing_qualifiers = ["const"]  # Keep const, remove &
    methods.append(get_method)

    # get_mutable_{field}() - clear ref-qualifiers since ElementRef is designed to be used as temporary
    get_mutable_method = tap.create_get_mutable_method(field_name, "get_mutable", mutable_ref, object_policy_accessor)
    get_mutable_method.trailing_qualifiers = []  # Remove &
    methods.append(get_mutable_method)

    # set_{field}(new_value) - use ObjectPolicy for assignment
    set_method = tap.create_set_method(field_name, cpp_type, object_policy_accessor)
    set_method.trailing_qualifiers = []  # Remove &
    methods.append(set_method)

    return methods


def _create_optional_type(inner_type: types.CppTypeExpr) -> types.CppTemplateType:
    """Create an std::optional<T> type."""
    return types.CppTemplateType(
        include=[SystemHeader("optional")],
        template_name="optional",
        cpp_namespace="std",
        arguments=[inner_type],
    )


def _create_optional_base_accessors(
    field_name: str,
    cpp_type: types.CppTypeExpr,
    field_doc: str,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create base accessors for Optional fields (get_underlying, has_, reset_)."""
    underlying_accessor = f"get_underlying_{field_name}()"
    methods = []

    # get_underlying_{field}() -> Optional
    methods.extend(_create_get_underlying_accessors(cpp_type, field_name, field_doc, soa_accessor))

    # has_ and reset_ methods
    methods.append(tap.create_has_optional_method(field_name, underlying_accessor))
    methods.append(tap.create_reset_optional_method(field_name, underlying_accessor))

    return methods


def _create_optional_fixed_array_accessors(
    compiler_context: CompilerContext,
    field_name: str,
    value_type: typesys.Instantiation,
    underlying_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for Optional<FixedArray<T, N>> fields."""
    inner_value_type = value_type.arguments["type"]
    if not isinstance(inner_value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal. Received: {inner_value_type}."
        raise TypeError(msg)
    cpp_inner_value_type = tap.to_cpp_type(compiler_context, inner_value_type)
    size_value = value_type.arguments["size"]
    cpp_value_size = tap.value_to_cpp(size_value)
    assert isinstance(cpp_value_size, types.CppValue)

    const_span = types.SPAN.instantiate([types.const_qualify(cpp_inner_value_type, True), cpp_value_size])
    mutable_span = types.SPAN.instantiate([cpp_inner_value_type, cpp_value_size])

    methods = []
    methods.append(tap.create_value_span_optional_method(field_name, const_span, True, underlying_accessor))
    methods.append(tap.create_value_span_optional_method(field_name, mutable_span, False, underlying_accessor))
    methods.append(tap.create_set_optional_from_span_method(field_name, const_span, underlying_accessor))
    return methods


def _create_optional_var_array_accessors(
    compiler_context: CompilerContext,
    field_name: str,
    value_type: typesys.Instantiation,
    underlying_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for Optional<VarArray<T, N>> fields."""
    inner_value_type = value_type.arguments["type"]
    if not isinstance(inner_value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal. Received: {inner_value_type}."
        raise TypeError(msg)
    cpp_inner_value_type = tap.to_cpp_type(compiler_context, inner_value_type)

    const_span = types.SPAN.instantiate([types.const_qualify(cpp_inner_value_type, True)])
    mutable_span = types.SPAN.instantiate([cpp_inner_value_type])
    optional_const_span = _create_optional_type(const_span)

    methods = []
    methods.append(tap.create_value_span_optional_method(field_name, const_span, True, underlying_accessor))
    methods.append(tap.create_value_span_optional_method(field_name, mutable_span, False, underlying_accessor))
    methods.append(
        tap.create_try_set_optional_from_span_method(field_name, const_span, underlying_accessor, use_callsig=True)
    )
    methods.append(
        tap.create_try_set_optional_from_optional_span_method(
            field_name, optional_const_span, underlying_accessor, use_callsig=True
        )
    )
    return methods


def _create_optional_var_string_accessors(
    field_name: str,
    underlying_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for Optional<VarString<N>> fields."""
    const_span = types.STRING_VIEW
    mutable_span = types.SPAN.instantiate([types.CHAR])
    optional_const_span = _create_optional_type(const_span)

    methods = []
    methods.append(tap.create_value_span_optional_method(field_name, const_span, True, underlying_accessor))
    methods.append(tap.create_value_span_optional_method(field_name, mutable_span, False, underlying_accessor))
    methods.append(
        tap.create_try_set_optional_from_span_method(field_name, const_span, underlying_accessor, use_callsig=True)
    )
    methods.append(
        tap.create_try_set_optional_from_optional_span_method(
            field_name, optional_const_span, underlying_accessor, use_callsig=True
        )
    )
    return methods


def _create_optional_value_accessors(
    field_name: str,
    cpp_value_type: types.CppTypeExpr,
    underlying_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for Optional<T> where T is a primitive, subschema, or other instantiation."""
    l_value = types.ref_qualify(cpp_value_type, types.Ref.L)
    const_l_value = types.const_qualify(l_value, True)
    optional_cpp_value_type = _create_optional_type(cpp_value_type)

    methods = []
    methods.append(tap.create_value_optional_method(field_name, const_l_value, True, "value", underlying_accessor))
    methods.append(tap.create_value_optional_method(field_name, l_value, False, "value", underlying_accessor))
    methods.append(tap.create_set_optional_method(field_name, cpp_value_type, underlying_accessor))
    methods.append(
        tap.create_set_optional_from_optional_method(field_name, optional_cpp_value_type, underlying_accessor)
    )
    return methods


def _create_optional_field_accessors(
    compiler_context: CompilerContext,
    field: schema.InstantiatedFieldDef,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Create accessors for Optional fields.

    This mirrors the logic from tap.py's _field_optional_to_cpp to generate the same
    rich set of accessors for Optional fields in SoA ElementRef.
    """
    field_type = field.type_info
    field_name = field.cur_name
    field_doc = field.doc.value

    if not isinstance(field_type, typesys.Instantiation) or field_type.instantiates != clkbuiltins.OPTIONAL:
        msg = f"Expected an Optional field type. Received: {field_type}"
        raise TypeError(msg)

    cpp_type = tap.to_cpp_type(compiler_context, field_type)
    value_type = field_type.arguments["type"]

    if not isinstance(value_type, typesys.TypeVal):
        msg = f"Expected a TypeVal. Received: {value_type}."
        raise TypeError(msg)

    cpp_value_type = tap.to_cpp_type(compiler_context, value_type)
    underlying_accessor = f"get_underlying_{field_name}()"

    methods = _create_optional_base_accessors(field_name, cpp_type, field_doc, soa_accessor)

    # Special methods for special element types
    if isinstance(value_type, typesys.Instantiation):
        if value_type.instantiates == clkbuiltins.FIXED_ARRAY:
            methods.extend(
                _create_optional_fixed_array_accessors(compiler_context, field_name, value_type, underlying_accessor)
            )
        elif value_type.instantiates == clkbuiltins.VAR_ARRAY:
            methods.extend(
                _create_optional_var_array_accessors(compiler_context, field_name, value_type, underlying_accessor)
            )
        elif value_type.instantiates == clkbuiltins.VAR_STRING:
            methods.extend(_create_optional_var_string_accessors(field_name, underlying_accessor))
        else:
            methods.extend(_create_optional_value_accessors(field_name, cpp_value_type, underlying_accessor))
    else:
        methods.extend(_create_optional_value_accessors(field_name, cpp_value_type, underlying_accessor))

    return methods


def _generate_element_ref_field_accessors(
    compiler_context: CompilerContext,
    field: schema.InstantiatedFieldDef,
    soa_accessor: str,
) -> list[types.CppMethod]:
    """Generate field accessor methods for ElementRef classes.

    Args:
        compiler_context: Compiler context
        field: Field definition
        soa_accessor: Expression to access the field array (e.g., "soa_->field_[index_]")

    Returns:
        List of accessor methods (get, get_mutable, set, etc.)
    """
    field_info = _get_field_accessor_info(compiler_context, field)
    field_name = field.cur_name
    field_doc = field.doc.value

    if field_info.is_fixed_array:
        return _create_fixed_array_field_accessors(field_info, field_name, soa_accessor)
    if field_info.is_var_array:
        return _create_var_array_field_accessors(field_info, field_name, field_doc, soa_accessor)
    if field_info.is_var_string:
        return _create_var_string_field_accessors(field_info, field_name, field_doc, soa_accessor)
    if field_info.is_optional:
        return _create_optional_field_accessors(compiler_context, field, soa_accessor)
    return _create_regular_field_accessors(field_info.cpp_type, field_name, soa_accessor)


def _add_element_ref_header_fields(
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    is_const: bool,
) -> None:
    """Add field accessor declarations to ElementRef header."""
    header.append("  // Field accessors")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        cpp_type = field_info.cpp_type
        assert isinstance(cpp_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
        field_name = field.cur_name

        if field_info.is_var_array:
            element_type = field_info.element_type
            assert isinstance(element_type, (types.CppType, types.CppTemplateType, types.CppScopedType))
            header.append(f"  [[nodiscard]] ::std::span<const {element_type.render('')}> get_{field_name}() const;")
            if not is_const:
                header.append(f"  [[nodiscard]] ::std::span<{element_type.render('')}> get_mutable_{field_name}();")
            header.append(f"  [[nodiscard]] const {cpp_type.render('')}& get_underlying_{field_name}() const &;")
            if not is_const:
                header.append(f"  [[nodiscard]] {cpp_type.render('')}& get_underlying_{field_name}() &;")
                header.append(
                    f"  ::jewels::BinaryOutcome try_set_{field_name}(::std::span<const {element_type.render('')}> input_span) &;"
                )
        else:
            header.append(f"  [[nodiscard]] const {cpp_type.render('')}& get_{field_name}() const;")
            if not is_const:
                header.append(f"  [[nodiscard]] {cpp_type.render('')}& get_mutable_{field_name}();")
                header.append(f"  void set_{field_name}({cpp_type.render('')} new_value);")

    header.append("")


def _add_element_ref_header_operators(  # noqa: PLR0913 too many args mitigated by kwonly args
    header: CppChunk,
    *,
    class_name: str,
    schema_name: str,
    schema_cpp_type: types.CppTypeExpr,
    soa_template_name: str,
    size_param_name: str,
    tap_type: types.CppTypeExpr,
    tap_init_type: types.CppTypeExpr,
    is_const: bool,
) -> None:
    """Add operator declarations to ElementRef header."""
    schema_type_str = schema_cpp_type.render("")
    if not is_const:
        header.append("  // Assignment operators")
        header.append(f"  {class_name}& operator=(const {tap_type.render('')}& tap);")
        header.append(f"  {class_name}& operator=(const {class_name}& other);")
        header.append(f"  {class_name}& operator=({class_name}&& other);")
        header.append(f"  {class_name}& operator=(const {tap_init_type.render('')}& init);")
        header.append("")
    else:
        header.append("  // Assignment operators deleted")
        header.append(f"  void operator=(const {tap_type.render('')}&) = delete;")
        header.append(
            f"  void operator=(const {schema_name}_{soa_template_name}ElementRef<{schema_type_str}, {size_param_name}>&) = delete;"
        )
        header.append(f"  void operator=(const {class_name}&) = delete;")
        header.append(f"  void operator=({class_name}&&) = delete;")
        header.append(f"  void operator=(const {tap_init_type.render('')}&) = delete;")
        header.append("")

    header.append("  // Conversion to Tap<Tachyon<SoaTestSchema>>")
    header.append(f"  explicit operator {tap_type.render('')}() const;")
    header.append("")

    header.append(f"  bool operator==(const {class_name}& other) const;")
    header.append("")


def _add_element_ref_swap_functions(
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    class_name: str,
) -> None:
    """Add swap friend functions to ElementRef header (non-const only)."""
    header.append("  // Swap for lvalue references (called by std::swap)")
    header.append(f"  friend void swap({class_name}& ref_a, {class_name}& ref_b) noexcept")
    header.append("  {")
    # Swap implementation needs to be inline in the class body (as a hidden friend) to make ADL work
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            header.append(f"    const auto temp_{field.cur_name} = ref_a.get_underlying_{field.cur_name}();")
        else:
            header.append(f"    const auto temp_{field.cur_name} = ref_a.get_{field.cur_name}();")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            header.append(f"    ref_a.get_underlying_{field.cur_name}() = ref_b.get_underlying_{field.cur_name}();")
        else:
            header.append(f"    ref_a.get_mutable_{field.cur_name}() = ref_b.get_{field.cur_name}();")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            header.append(f"    ref_b.get_underlying_{field.cur_name}() = temp_{field.cur_name};")
        else:
            header.append(f"    ref_b.get_mutable_{field.cur_name}() = temp_{field.cur_name};")
    header.append("  }")
    header.append("")
    header.append("  // Swap for rvalue references (called by std::iter_swap via *it)")
    header.append("  friend void swap(")
    header.append(f"    {class_name}&& ref_a,           // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)")
    header.append(f"    {class_name}&& ref_b) noexcept  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)")
    header.append("  {")
    header.append("    swap(ref_a, ref_b);")
    header.append("  }")
    header.append("")


def _generate_element_ref_impl_accessors(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    class_name: str,
    size_param_name: str,
    is_const: bool,
    schema_cpp_type: types.CppTypeExpr,
) -> None:
    """Generate field accessor method implementations."""
    schema_type_str = schema_cpp_type.render("")
    specialization = f"{class_name}<{schema_type_str}, {size_param_name}>"

    for field in schema_ir.fields.values():
        soa_accessor = f"soa_->{field.cur_name}_[index_]"
        accessors = _generate_element_ref_field_accessors(compiler_context, field, soa_accessor)
        for accessor in accessors:
            if is_const:
                if "get_mutable" in accessor.name or "set_" in accessor.name or "try_set" in accessor.name:
                    continue
                if "get_underlying" in accessor.name and "const" not in accessor.trailing_qualifiers:
                    continue

            impl.append(f"template <size_t {size_param_name}>")

            return_type_str = (
                accessor.return_type.render("")
                if hasattr(accessor.return_type, "render")
                else str(accessor.return_type)
            )

            args_list = [arg.render("") for arg in accessor.arguments] if accessor.arguments else []
            args_str = ", ".join(args_list)

            qualifiers = " ".join(accessor.trailing_qualifiers) if accessor.trailing_qualifiers else ""
            if qualifiers:
                qualifiers = " " + qualifiers
            impl.append(f"{return_type_str} {specialization}::{accessor.name}({args_str}){qualifiers}")
            impl.append("{")

            if accessor.body:
                for line in accessor.body.lines:
                    if line.strip():
                        impl.append(f"  {line}")

            impl.append("}")
            impl.append("")


def _generate_element_ref_impl_assignment_ops(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    class_name: str,
    size_param_name: str,
    tap_type: types.CppTypeExpr,
    tap_init_type: types.CppTypeExpr,
    schema_cpp_type: types.CppTypeExpr,
) -> None:
    """Generate assignment operator implementations (for non-const ElementRef only)."""
    schema_type_str = schema_cpp_type.render("")
    specialization = f"{class_name}<{schema_type_str}, {size_param_name}>"

    # Assignment from Tap
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}& {specialization}::operator=(const {tap_type.render('')}& tap)")
    impl.append("{")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            impl.append(f"  get_underlying_{field.cur_name}() = tap.get_underlying_{field.cur_name}();")
        else:
            impl.append(f"  get_mutable_{field.cur_name}() = tap.get_{field.cur_name}();")
    impl.append("  return *this;")
    impl.append("}")
    impl.append("")

    # Assignment from another ElementRef
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}& {specialization}::operator=(const {class_name}& other)")
    impl.append("{")
    impl.append("  if (this != &other)")
    impl.append("  {")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            impl.append(f"    get_underlying_{field.cur_name}() = other.get_underlying_{field.cur_name}();")
        else:
            impl.append(f"    get_mutable_{field.cur_name}() = other.get_{field.cur_name}();")
    impl.append("  }")
    impl.append("  return *this;")
    impl.append("}")
    impl.append("")

    # Move assignment
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}& {specialization}::operator=({class_name}&& other)")
    impl.append("{")
    impl.append("  return *this = other;")
    impl.append("}")
    impl.append("")

    # Assignment from TapInit
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}& {specialization}::operator=(const {tap_init_type.render('')}& init)")
    impl.append("{")
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            impl.append(f"  get_underlying_{field.cur_name}() = init.{field.cur_name};")
        else:
            impl.append(f"  get_mutable_{field.cur_name}() = init.{field.cur_name};")
    impl.append("  return *this;")
    impl.append("}")
    impl.append("")


def _generate_element_ref_impl_conversion_and_equality(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    class_name: str,
    size_param_name: str,
    tap_type: types.CppTypeExpr,
    tap_init_type: types.CppTypeExpr,
    schema_cpp_type: types.CppTypeExpr,
) -> None:
    """Generate conversion and equality operator implementations."""
    schema_type_str = schema_cpp_type.render("")
    specialization = f"{class_name}<{schema_type_str}, {size_param_name}>"

    # Conversion operator to Tap
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}::operator {tap_type.render('')}() const")
    impl.append("{")
    impl.append(f"  return {tap_type.render('')}{{")
    impl.append(f"    {tap_init_type.render('')}{{")

    init_list = []
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            init_list.append(f"      .{field.cur_name} = get_underlying_{field.cur_name}()")
        else:
            init_list.append(f"      .{field.cur_name} = get_{field.cur_name}()")
    impl.append(",\n".join(init_list) + "}};")
    impl.append("}")
    impl.append("")

    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"bool {specialization}::operator==(const {class_name}& other) const")
    impl.append("{")
    comparisons = []
    for field in schema_ir.fields.values():
        field_info = _get_field_accessor_info(compiler_context, field)
        if field_info.is_var_array:
            comparisons.append(f"get_underlying_{field.cur_name}() == other.get_underlying_{field.cur_name}()")
        else:
            comparisons.append(f"get_{field.cur_name}() == other.get_{field.cur_name}()")
    impl.append(f"  return {' && '.join(comparisons)};")
    impl.append("}")
    impl.append("")


@dataclass(slots=True, frozen=True)
class _ElementRefTypeInfo:
    """Type information needed for generating ElementRef classes."""

    schema_cpp_type: types.CppTypeExpr
    schema_name: str
    class_name: str
    soa_type_rendered: str
    soa_ptr_type: str
    tap_type: types.CppTypeExpr
    tap_init_type: types.CppTypeExpr
    non_const_class: str | None  # Only for ElementConstRef
    const_class: str | None  # Only for ElementRef


def _build_element_ref_type_info(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    soa_template_name: str,
    size_param_name: str,
    is_const: bool,
) -> _ElementRefTypeInfo:
    """Build all type information needed for ElementRef class generation."""
    schema_cpp_type = typereg.get_cpp_type(compiler_context, schema_ir.as_instantiation_or_resolved_schema())
    schema_name = schema_ir.schema_name
    suffix = "ElementConstRef" if is_const else "ElementRef"
    class_name = f"{schema_name}_{soa_template_name}{suffix}"

    # Tap<Tachyon<T>>
    tachyon_inst = typesys.Instantiation(
        instantiates=clkbuiltins.TACHYON,
        arguments={"schema": schema_ir.as_instantiation_or_resolved_schema()},
        type_info=clkbuiltins.TYPE_TYPE,
    )
    tap_inst = typesys.Instantiation(
        instantiates=clkbuiltins.TAP,
        arguments={"representation": tachyon_inst},
        type_info=clkbuiltins.TYPE_TYPE,
    )
    tap_type = typereg.get_cpp_type(compiler_context, tap_inst)

    # TapInit<Tachyon<T>>
    tap_init_inst = typesys.Instantiation(
        instantiates=clkbuiltins.TAP_INIT,
        arguments={"representation": tachyon_inst},
        type_info=clkbuiltins.TYPE_TYPE,
    )
    tap_init_type = typereg.get_cpp_type(compiler_context, tap_init_inst)

    # SoA pointer type
    soa_type_rendered = f"::clockwork::{soa_template_name}<{schema_cpp_type.render('')}, {size_param_name}>"
    soa_ptr_type = f"const {soa_type_rendered}*" if is_const else f"{soa_type_rendered}*"

    # ElementRef class names
    schema_type_str = schema_cpp_type.render("")
    non_const_class = (
        f"{schema_name}_{soa_template_name}ElementRef<{schema_type_str}, {size_param_name}>" if is_const else None
    )
    const_class = (
        f"{schema_name}_{soa_template_name}ElementConstRef<{schema_type_str}, {size_param_name}>"
        if not is_const
        else None
    )

    return _ElementRefTypeInfo(
        schema_cpp_type=schema_cpp_type,
        schema_name=schema_name,
        class_name=class_name,
        soa_type_rendered=soa_type_rendered,
        soa_ptr_type=soa_ptr_type,
        tap_type=tap_type,
        tap_init_type=tap_init_type,
        non_const_class=non_const_class,
        const_class=const_class,
    )


def _generate_element_ref_header(  # noqa: PLR0913 # too many args mitigated by kwonly args
    *,
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    type_info: _ElementRefTypeInfo,
    size_param_name: str,
    soa_template_name: str,
    is_const: bool,
) -> None:
    """Generate the header declaration for ElementRef class."""
    schema_type_str = type_info.schema_cpp_type.render("")
    header.append(
        f"/// Proxy {'const ' if is_const else ''}reference to access a single {type_info.schema_name} element in a SoA"
    )
    header.append(f"template <size_t {size_param_name}>")
    header.append(f"class {type_info.class_name}<{schema_type_str}, {size_param_name}>")
    header.append("{")
    header.append("public:")

    if not is_const and type_info.const_class:
        header.append(f"  friend class {type_info.const_class};")
        header.append("")

    header.append(f"  {type_info.class_name}({type_info.soa_ptr_type} soa, size_t index);")

    if is_const and type_info.non_const_class:
        header.append(f"  explicit {type_info.class_name}(const {type_info.non_const_class}& ref);")
    header.append(f"  {type_info.class_name}(const {type_info.class_name}&) = default;")
    header.append(f"  {type_info.class_name}({type_info.class_name}&&) = default;")
    header.append(f"  ~{type_info.class_name}() = default;")
    header.append("")

    _add_element_ref_header_fields(header, schema_ir, compiler_context, is_const)

    _add_element_ref_header_operators(
        header,
        class_name=type_info.class_name,
        schema_name=type_info.schema_name,
        schema_cpp_type=type_info.schema_cpp_type,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        tap_type=type_info.tap_type,
        tap_init_type=type_info.tap_init_type,
        is_const=is_const,
    )

    if not is_const:
        _add_element_ref_swap_functions(header, schema_ir, compiler_context, type_info.class_name)

    header.append("private:")
    header.append(f"  {type_info.soa_ptr_type} soa_;")
    header.append("  size_t index_;")
    header.append("};")
    header.append("")


def _generate_element_ref_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    *,
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    type_info: _ElementRefTypeInfo,
    size_param_name: str,
    is_const: bool,
) -> None:
    """Generate the implementation for ElementRef class."""
    schema_type_str = type_info.schema_cpp_type.render("")
    specialization = f"{type_info.class_name}<{schema_type_str}, {size_param_name}>"

    # Constructor
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"{specialization}::{type_info.class_name}({type_info.soa_ptr_type} soa, size_t index)")
    impl.append("  : soa_{soa}, index_{index}")
    impl.append("{")
    impl.append("}")
    impl.append("")

    # For ElementConstRef, add constructor from ElementRef
    if is_const and type_info.non_const_class:
        impl.append(f"template <size_t {size_param_name}>")
        impl.append(f"{specialization}::{type_info.class_name}(const {type_info.non_const_class}& ref)")
        impl.append("  : soa_{ref.soa_}, index_{ref.index_}")
        impl.append("{")
        impl.append("}")
        impl.append("")

    _generate_element_ref_impl_accessors(
        impl,
        schema_ir,
        compiler_context,
        class_name=type_info.class_name,
        size_param_name=size_param_name,
        is_const=is_const,
        schema_cpp_type=type_info.schema_cpp_type,
    )

    if not is_const:
        _generate_element_ref_impl_assignment_ops(
            impl,
            schema_ir,
            compiler_context,
            class_name=type_info.class_name,
            size_param_name=size_param_name,
            tap_type=type_info.tap_type,
            tap_init_type=type_info.tap_init_type,
            schema_cpp_type=type_info.schema_cpp_type,
        )

    _generate_element_ref_impl_conversion_and_equality(
        impl,
        schema_ir,
        compiler_context,
        class_name=type_info.class_name,
        size_param_name=size_param_name,
        tap_type=type_info.tap_type,
        tap_init_type=type_info.tap_init_type,
        schema_cpp_type=type_info.schema_cpp_type,
    )


def _generate_std_swap_specialization(
    impl: CppChunk,
    type_info: _ElementRefTypeInfo,
    schema_namespace: str,
    size_param_name: str,
) -> None:
    """Generate std::swap specialization for ElementRef."""
    schema_type_str = type_info.schema_cpp_type.render("")
    impl.append("// std::swap specialization to call our custom swap")
    impl.append("namespace std")
    impl.append("{")
    impl.append(f"template <size_t {size_param_name}>")
    impl.append("inline void swap(")
    qualified_class = (
        f"::{schema_namespace}::{type_info.class_name}"
        if schema_namespace != types.GLOBAL_NAMESPACE
        else f"::{type_info.class_name}"
    )
    specialization = f"{qualified_class}<{schema_type_str}, {size_param_name}>"
    impl.append(f"  {specialization}& ref_a,")
    impl.append(f"  {specialization}& ref_b) noexcept")
    impl.append("{")
    namespace_desc = schema_namespace if schema_namespace != types.GLOBAL_NAMESPACE else "global"
    impl.append(f"  swap(ref_a, ref_b);  // Calls ADL swap in {namespace_desc} namespace")
    impl.append("}")
    impl.append("} // namespace std")
    impl.append("")


def _generate_element_ref_class(  # noqa: PLR0913 # too many args mitigated by kwonly args
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    *,
    schema_namespace: str,
    soa_template_name: str,
    size_param_name: str,
    is_const: bool,
) -> CppModuleChunks:
    """Generate ElementRef or ElementConstRef proxy class with implementation.

    Args:
        compiler_context: Compiler context
        schema_ir: Schema definition
        schema_namespace: The C++ namespace where the schema is defined
        soa_template_name: "FixedSoa" or "VarSoa"
        size_param_name: "size" or "max_size"
        is_const: True for ElementConstRef, False for ElementRef

    Returns:
        CppModuleChunks with the class declaration and implementation
    """
    type_info = _build_element_ref_type_info(compiler_context, schema_ir, soa_template_name, size_param_name, is_const)

    chunks = CppModuleChunks()
    header = chunks.header_chunk
    impl = chunks.inline_chunk

    _generate_element_ref_header(
        header=header,
        schema_ir=schema_ir,
        compiler_context=compiler_context,
        type_info=type_info,
        size_param_name=size_param_name,
        soa_template_name=soa_template_name,
        is_const=is_const,
    )

    if schema_namespace != types.GLOBAL_NAMESPACE:
        impl.append(f"namespace {schema_namespace}")
        impl.append("{")
        impl.append("")
    impl.append(f"// {type_info.class_name} implementations")
    _generate_element_ref_impl(
        impl=impl,
        schema_ir=schema_ir,
        compiler_context=compiler_context,
        type_info=type_info,
        size_param_name=size_param_name,
        is_const=is_const,
    )
    if schema_namespace != types.GLOBAL_NAMESPACE:
        impl.append(f"}} // namespace {schema_namespace}")
    impl.append("")

    if not is_const:
        _generate_std_swap_specialization(impl, type_info, schema_namespace, size_param_name)

    return chunks


def _add_soa_view_method_declarations(
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
) -> None:
    """Add view method declarations to SoA struct header."""
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name

        header.append(f"  /// Return a span view of the '{field_name}' field array.")
        header.append(f"  [[nodiscard]] inline ::std::span<{storage_type.render('')}> view_{field_name}();")

        header.append(f"  /// Return a const span view of the '{field_name}' field array.")
        header.append(f"  [[nodiscard]] inline ::std::span<const {storage_type.render('')}> view_{field_name}() const;")


def _add_soa_private_field_declarations(
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    layout_info: tachyon_reg.SoaLayoutInfo,
    compiler_context: CompilerContext,
    size_param_name: str,
) -> None:
    """Add private field array member declarations to SoA struct header."""
    for field_layout in layout_info.field_layouts:
        if field_layout.field_num == tachyon_reg.SIZE_FIELD_NUM:
            header.append("  /// Number of elements currently in the SoA.")
            header.append(f"  ::jewels::tap::compact_size_t<{size_param_name}> size_{{}};")
        else:
            field = schema_ir.fields[field_layout.field_num]
            storage_type = tap.to_cpp_type(compiler_context, field.type_info)
            doc_lines = comment_doc_string(f"Array storage for field '{field.cur_name}': {field.doc.value}")
            for line in doc_lines:
                header.append(f"  {line}")
            header.append(
                f"  ::std::array<::jewels::memory::AlignedStorage<{storage_type.render('')}>, {size_param_name}> {field.cur_name}_{{}};"
            )


def _generate_soa_struct_header_decl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    header: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    layout_info: tachyon_reg.SoaLayoutInfo,
    compiler_context: CompilerContext,
    *,
    soa_template_name: str,
    size_param_name: str,
    include_size_field: bool,
    soa_full_type: str,
    base_class: str,
    schema_name: str,
    schema_cpp_type: types.CppTypeExpr,
    element_ref_type: types.CppType,
    element_const_ref_type: types.CppType,
) -> None:
    """Generate the struct declaration in the header."""
    header.append("#pragma clang diagnostic push")
    header.append('#pragma clang diagnostic ignored "-Wpacked-non-pod"')
    header.append(f"/// {'Variable' if include_size_field else 'Fixed'}-size Struct-of-Arrays for {schema_name}.")
    header.append(f"template <size_t {size_param_name}>")
    header.append(f"struct __attribute__((__packed__)) alignas({layout_info.max_alignment})")
    header.append(f"  {soa_template_name}<{schema_cpp_type.render('')}, {size_param_name}> : public {base_class}")
    header.append("{")
    header.append("public:")
    header.append(f"  using Base = {base_class};")

    clockwork_namespace = "clockwork"
    schema_type_str = schema_cpp_type.render("")
    header.append(
        f"  using ElementRef = {element_ref_type.render(clockwork_namespace)}<{schema_type_str}, {size_param_name}>;"
    )
    header.append(
        f"  using ElementConstRef = {element_const_ref_type.render(clockwork_namespace)}<{schema_type_str}, {size_param_name}>;"
    )
    header.append("  friend Base;")
    header.append(f"  friend {element_ref_type.render(clockwork_namespace)}<{schema_type_str}, {size_param_name}>;")
    header.append(
        f"  friend {element_const_ref_type.render(clockwork_namespace)}<{schema_type_str}, {size_param_name}>;"
    )
    header.append("")
    header.append(f"  {soa_template_name}() = default;")
    header.append("")

    _add_soa_view_method_declarations(header, schema_ir, compiler_context)

    header.append("")

    header.append("  /// Compile-time layout verification (accesses private members for static_assert).")
    header.append(f"  /// Only valid for {size_param_name} == 1.")
    header.append(f"  static constexpr void verify_layout() noexcept requires ({size_param_name} == 1);")
    header.append("")
    header.append("private:")

    _add_soa_private_field_declarations(header, schema_ir, layout_info, compiler_context, size_param_name)

    header.append("")

    header.append("  /// Compare field data (CRTP method for SoaInterface).")
    header.append(f"  inline bool compare_fields(const {soa_full_type}& other, size_t count) const;")
    header.append("")

    header.append("  void wipe_range(size_t begin, size_t end);")
    header.append("  void construct_range(size_t begin, size_t end);")
    header.append("  void construct_element(size_t index);")
    header.append(
        f"  void construct_element(size_t index, const ::clockwork::Tap<::clockwork::Tachyon<{schema_cpp_type.render('')}>>& tap);"
    )
    header.append("  void construct_element(size_t index, const ElementRef& ref);")
    header.append(
        f"  void construct_element(size_t index, const ::clockwork::TapInit<::clockwork::Tachyon<{schema_cpp_type.render('')}>>& init);"
    )

    header.append("};")
    header.append("#pragma clang diagnostic pop")
    header.append("")


def _generate_soa_view_methods_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    soa_template_name: str,
    size_param_name: str,
    include_size_field: bool,
    soa_full_type: str,
) -> None:
    """Generate view method implementations."""
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name

        # Non-const view implementation
        impl.append(f"// {soa_template_name} implementations")
        impl.append(f"template <size_t {size_param_name}>")
        impl.append(f"inline auto {soa_full_type}::view_{field_name}() -> ::std::span<{storage_type.render('')}>")
        impl.append("{")
        if include_size_field:
            impl.append(
                f"  return std::span<{storage_type.render('')}>(::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr({field_name}_[0]), size_);"
            )
        else:
            impl.append(
                f"  return std::span<{storage_type.render('')}>(::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr({field_name}_[0]), {size_param_name});"
            )
        impl.append("}")
        impl.append("")

        # Const view implementation
        impl.append(f"template <size_t {size_param_name}>")
        impl.append(
            f"inline auto {soa_full_type}::view_{field_name}() const -> ::std::span<const {storage_type.render('')}>"
        )
        impl.append("{")
        if include_size_field:
            impl.append(
                f"  return std::span<const {storage_type.render('')}>(::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr({field_name}_[0]), size_);"
            )
        else:
            impl.append(
                f"  return std::span<const {storage_type.render('')}>(::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr({field_name}_[0]), {size_param_name});"
            )
        impl.append("}")
        impl.append("")


def _generate_soa_compare_fields_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    soa_template_name: str,
    size_param_name: str,
    soa_full_type: str,
) -> None:
    """Generate compare_fields method implementation."""
    impl.append(f"// compare_fields implementation for {soa_template_name}")
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"inline auto {soa_full_type}::compare_fields(")
    impl.append(f"  const {soa_full_type}& other, size_t count) const -> bool")
    impl.append("{")
    impl.append("  if (count == 0)")
    impl.append("  {")
    impl.append("    return true;")
    impl.append("  }")

    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(
            f"  auto {field_name}_this = std::span{{::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr({field_name}_[0]), count}};"
        )
        impl.append(
            f"  auto {field_name}_other = std::span{{::jewels::memory::ObjectPolicy<{storage_type.render('')}>::ptr(other.{field_name}_[0]), count}};"
        )

    impl.append("")
    impl.append(
        "  return "
        + " &&\n         ".join(
            f"std::equal({field.cur_name}_this.begin(), {field.cur_name}_this.end(), {field.cur_name}_other.begin())"
            for field in schema_ir.fields.values()
        )
        + ";"
    )
    impl.append("}")
    impl.append("")


def _generate_soa_wipe_range_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    soa_template_name: str,
    size_param_name: str,
    soa_full_type: str,
) -> None:
    """Generate wipe_range method implementation."""
    impl.append(f"// wipe_range implementation for {soa_template_name}")
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::wipe_range(size_t begin, size_t end)")
    impl.append("{")
    impl.append("  if (begin >= end)")
    impl.append("  {")
    impl.append("    return;")
    impl.append("  }")
    impl.append("  // Zero out the range")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(f"  auto {field_name}_bytes = std::as_writable_bytes(std::span{{{field_name}_}});")
    impl.append("")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append("  std::fill(")
        impl.append(
            f"    {field_name}_bytes.subspan(begin * sizeof({storage_type.render('')}), (end - begin) * sizeof({storage_type.render('')})).begin(),"
        )
        impl.append(
            f"    {field_name}_bytes.subspan(begin * sizeof({storage_type.render('')}), (end - begin) * sizeof({storage_type.render('')})).end(),"
        )
        impl.append("    std::byte{0});")
    impl.append("}")
    impl.append("")


def _generate_soa_construct_methods_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    compiler_context: CompilerContext,
    *,
    soa_template_name: str,
    size_param_name: str,
    soa_full_type: str,
    schema_cpp_type: types.CppTypeExpr,
    element_ref_type: types.CppType,
) -> None:
    """Generate construct_range and construct_element method implementations."""
    impl.append(f"// construct_range implementation for {soa_template_name}")
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::construct_range(size_t begin, size_t end)")
    impl.append("{")
    impl.append("  for (size_t i = begin; i < end; ++i)")
    impl.append("  {")
    impl.append("    construct_element(i);")
    impl.append("  }")
    impl.append("}")
    impl.append("")

    impl.append(f"// construct_element implementations for {soa_template_name}")

    # Default constructor
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::construct_element(size_t index)")
    impl.append("{")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(f"  ::jewels::memory::ObjectPolicy<{storage_type.render('')}>::construct({field_name}_[index]);")
    impl.append("}")
    impl.append("")

    # From Tap<Tachyon<T>>
    tap_type_str = f"::clockwork::Tap<::clockwork::Tachyon<{schema_cpp_type.render('')}>>"
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::construct_element(size_t index, const {tap_type_str}& tap)")
    impl.append("{")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(
            f"  ::jewels::memory::ObjectPolicy<{storage_type.render('')}>::construct({field_name}_[index], tap.get_{field_name}());"
        )
    impl.append("}")
    impl.append("")

    # From ElementRef
    schema_type_str = schema_cpp_type.render("")
    element_ref_type_str = f"{element_ref_type.render('')}<{schema_type_str}, {size_param_name}>"
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::construct_element(size_t index, const {element_ref_type_str}& ref)")
    impl.append("{")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(
            f"  ::jewels::memory::ObjectPolicy<{storage_type.render('')}>::construct({field_name}_[index], ref.get_{field_name}());"
        )
    impl.append("}")
    impl.append("")

    # From TapInit
    tap_init_type_str = f"::clockwork::TapInit<::clockwork::Tachyon<{schema_cpp_type.render('')}>>"
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"void {soa_full_type}::construct_element(size_t index, const {tap_init_type_str}& init)")
    impl.append("{")
    for field in schema_ir.fields.values():
        storage_type = tap.to_cpp_type(compiler_context, field.type_info)
        field_name = field.cur_name
        impl.append(
            f"  ::jewels::memory::ObjectPolicy<{storage_type.render('')}>::construct({field_name}_[index], init.{field_name});"
        )
    impl.append("}")
    impl.append("")


def _generate_soa_verify_layout_impl(  # noqa: PLR0913 # too many args mitigated by kwonly args
    impl: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    layout_info: tachyon_reg.SoaLayoutInfo,
    *,
    soa_template_name: str,
    size_param_name: str,
    soa_full_type: str,
) -> None:
    """Generate static layout verification method implementation."""
    impl.append(f"// Layout verification for {soa_template_name}")
    impl.append(f"template <size_t {size_param_name}>")
    impl.append(f"constexpr void {soa_full_type}::verify_layout() noexcept requires ({size_param_name} == 1)")
    impl.append("{")

    impl.append(f"  using SoaType = {soa_full_type};")
    impl.append("")

    for field_layout in layout_info.field_layouts:
        if field_layout.field_num == tachyon_reg.SIZE_FIELD_NUM:
            impl.append(f'  static_assert(offsetof(SoaType, size_) == {field_layout.offset}, "size_ offset mismatch");')
        else:
            field = schema_ir.fields[field_layout.field_num]
            field_member_name = f"{field.cur_name}_"
            assert_msg = f"Field {field.cur_name} offset mismatch"
            impl.append(
                f'  static_assert(offsetof(SoaType, {field_member_name}) == {field_layout.offset}, "{assert_msg}");'
            )

    impl.append(f"  static_assert(sizeof(SoaType) == {layout_info.total_size});")
    impl.append(f"  static_assert(alignof(SoaType) == {layout_info.max_alignment});")
    impl.append("}")
    impl.append("")


def _generate_soa_struct(  # noqa: PLR0913 # too many args mitigated by kwonly args
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    layout_info: tachyon_reg.SoaLayoutInfo,
    *,
    soa_template_name: str,
    size_param_name: str,
    include_size_field: bool,
    element_ref_type: types.CppType,
    element_const_ref_type: types.CppType,
) -> CppModuleChunks:
    """Generate FixedSoa or VarSoa struct definition.

    Args:
        compiler_context: Compiler context
        schema_ir: Schema definition
        layout_info: Layout information
        soa_template_name: "FixedSoa" or "VarSoa"
        size_param_name: "size" or "max_size"
        include_size_field: True for VarSoa, False for FixedSoa
        element_ref_type: CppType for the ElementRef class
        element_const_ref_type: CppType for the ElementConstRef class

    Returns:
        CppModuleChunks with struct declaration and implementation
    """
    chunks = CppModuleChunks()
    header = chunks.header_chunk
    impl = chunks.inline_chunk

    header.context.add_includes(types.BINARY_OUTCOME.includes)
    header.context.add_include(types.Header(types.JEWELS_REPO, "jewels/container/tap/soa.hh"))
    header.context.add_include(types.Header(types.JEWELS_REPO, "jewels/memory/aligned_storage.hh"))

    schema_cpp_type = typereg.get_cpp_type(compiler_context, schema_ir.as_instantiation_or_resolved_schema())
    schema_name = schema_ir.schema_name

    soa_full_type = f"::clockwork::{soa_template_name}<{schema_cpp_type.render('')}, {size_param_name}>"

    # Base class: SoaInterface<Derived, ElementType, capacity, is_variable>
    is_variable_str = "true" if include_size_field else "false"
    base_class = f"::jewels::tap::SoaInterface<{soa_full_type}, {schema_cpp_type.render('')}, {size_param_name}, {is_variable_str}>"

    _generate_soa_struct_header_decl(
        header,
        schema_ir,
        layout_info,
        compiler_context,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        include_size_field=include_size_field,
        soa_full_type=soa_full_type,
        base_class=base_class,
        schema_name=schema_name,
        schema_cpp_type=schema_cpp_type,
        element_ref_type=element_ref_type,
        element_const_ref_type=element_const_ref_type,
    )

    _generate_soa_view_methods_impl(
        impl,
        schema_ir,
        compiler_context,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        include_size_field=include_size_field,
        soa_full_type=soa_full_type,
    )

    _generate_soa_compare_fields_impl(
        impl,
        schema_ir,
        compiler_context,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        soa_full_type=soa_full_type,
    )

    _generate_soa_wipe_range_impl(
        impl,
        schema_ir,
        compiler_context,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        soa_full_type=soa_full_type,
    )

    _generate_soa_construct_methods_impl(
        impl,
        schema_ir,
        compiler_context,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        soa_full_type=soa_full_type,
        schema_cpp_type=schema_cpp_type,
        element_ref_type=element_ref_type,
    )

    _generate_soa_verify_layout_impl(
        impl,
        schema_ir,
        layout_info,
        soa_template_name=soa_template_name,
        size_param_name=size_param_name,
        soa_full_type=soa_full_type,
    )

    return chunks


def _generate_soa_traits_specializations(  # noqa: PLR0913 # too many args mitigated by kwonly args
    header: CppChunk,
    schema_cpp_type: types.CppTypeExpr,
    fixed_ref_type: types.CppType,
    fixed_const_ref_type: types.CppType,
    var_ref_type: types.CppType,
    var_const_ref_type: types.CppType,
) -> None:
    """Generate SoaTraits specializations in jewels::tap namespace."""
    header.append("// Specialize SoaTraits in jewels::tap namespace to break CRTP circular dependency")
    header.append("namespace jewels::tap")
    header.append("{")
    header.append("")

    traits_namespace = "jewels::tap"
    schema_type_str = schema_cpp_type.render("")

    header.append("template <size_t size>")
    header.append(f"struct SoaTraits<::clockwork::FixedSoa<{schema_type_str}, size>>")
    header.append("{")
    header.append(f"  using ElementRef = {fixed_ref_type.render(traits_namespace)}<{schema_type_str}, size>;")
    header.append(
        f"  using ElementConstRef = {fixed_const_ref_type.render(traits_namespace)}<{schema_type_str}, size>;"
    )
    header.append("};")
    header.append("")

    header.append("template <size_t max_size>")
    header.append(f"struct SoaTraits<::clockwork::VarSoa<{schema_type_str}, max_size>>")
    header.append("{")
    header.append(f"  using ElementRef = {var_ref_type.render(traits_namespace)}<{schema_type_str}, max_size>;")
    header.append(
        f"  using ElementConstRef = {var_const_ref_type.render(traits_namespace)}<{schema_type_str}, max_size>;"
    )
    header.append("};")
    header.append("")

    header.append("} // namespace jewels::tap")
    header.append("")


def _generate_element_ref_types(
    schema_ir: schema.InstantiatedSchema,
    schema_namespace: str,
) -> tuple[types.CppType, types.CppType, types.CppType, types.CppType]:
    """Create CppType objects for the ElementRef classes.

    Returns:
        Tuple of (fixed_ref_type, fixed_const_ref_type, var_ref_type, var_const_ref_type)
    """
    schema_name = schema_ir.schema_name

    fixed_ref_type = types.CppType(
        includes=[],
        type_name=f"{schema_name}_FixedSoaElementRef",
        cpp_namespace=schema_namespace,
    )
    fixed_const_ref_type = types.CppType(
        includes=[],
        type_name=f"{schema_name}_FixedSoaElementConstRef",
        cpp_namespace=schema_namespace,
    )
    var_ref_type = types.CppType(
        includes=[],
        type_name=f"{schema_name}_VarSoaElementRef",
        cpp_namespace=schema_namespace,
    )
    var_const_ref_type = types.CppType(
        includes=[],
        type_name=f"{schema_name}_VarSoaElementConstRef",
        cpp_namespace=schema_namespace,
    )

    return fixed_ref_type, fixed_const_ref_type, var_ref_type, var_const_ref_type


def _generate_all_element_ref_classes(
    cpp_mod: CppModuleChunks,
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    schema_namespace: str,
    schema_name: str,
) -> None:
    """Generate all four ElementRef classes (Fixed/Var x Ref/ConstRef) and append to cpp_mod."""
    header = cpp_mod.header_chunk

    header.append(f"namespace {schema_namespace}")
    header.append("{")
    header.append("")

    # Primary templates (no implementation) - only generated once per schema name
    # These forward declare the two-parameter template that specializations will fill in
    header.append(f"template <class SchemaType, size_t size> class {schema_name}_FixedSoaElementRef;")
    header.append(f"template <class SchemaType, size_t size> class {schema_name}_FixedSoaElementConstRef;")
    header.append(f"template <class SchemaType, size_t max_size> class {schema_name}_VarSoaElementRef;")
    header.append(f"template <class SchemaType, size_t max_size> class {schema_name}_VarSoaElementConstRef;")
    header.append("")

    fixed_ref_chunks = _generate_element_ref_class(
        compiler_context,
        schema_ir,
        schema_namespace=schema_namespace,
        soa_template_name="FixedSoa",
        size_param_name="size",
        is_const=False,
    )
    cpp_mod.append(fixed_ref_chunks)

    fixed_const_ref_chunks = _generate_element_ref_class(
        compiler_context,
        schema_ir,
        schema_namespace=schema_namespace,
        soa_template_name="FixedSoa",
        size_param_name="size",
        is_const=True,
    )
    cpp_mod.append(fixed_const_ref_chunks)

    var_ref_chunks = _generate_element_ref_class(
        compiler_context,
        schema_ir,
        schema_namespace=schema_namespace,
        soa_template_name="VarSoa",
        size_param_name="max_size",
        is_const=False,
    )
    cpp_mod.append(var_ref_chunks)

    var_const_ref_chunks = _generate_element_ref_class(
        compiler_context,
        schema_ir,
        schema_namespace=schema_namespace,
        soa_template_name="VarSoa",
        size_param_name="max_size",
        is_const=True,
    )
    cpp_mod.append(var_const_ref_chunks)

    header.append(f"}} // namespace {schema_namespace}")
    header.append("")


def _add_layout_verification_static_asserts(
    implementation_chunk: CppChunk,
    schema_ir: schema.InstantiatedSchema,
    schema_cpp_type: types.CppTypeExpr,
    layout_var: tachyon_reg.SoaLayoutInfo,
    enclosing_namespace: str,
) -> None:
    """Add static assertions for layout verification in implementation."""
    implementation_chunk.append("// Trigger compile-time layout verification")

    # FixedSoa verification
    soa_fqn_fixed = f"::clockwork::FixedSoa<{schema_cpp_type.render(enclosing_namespace)}, 1>"
    implementation_chunk.append(f"static_assert((({soa_fqn_fixed}::verify_layout()), true));")

    # VarSoa verification
    implementation_chunk.append(f"// Total size for VarSoa<{schema_ir.schema_name}, 1> = {layout_var.total_size} bytes")
    soa_fqn_var = f"::clockwork::VarSoa<{schema_cpp_type.render(enclosing_namespace)}, 1>"
    implementation_chunk.append(f"static_assert((({soa_fqn_var}::verify_layout()), true));")


def render_soa_types(
    compiler_context: CompilerContext,
    schema_ir: schema.InstantiatedSchema,
    enclosing_namespace: str,
) -> CppModuleChunks:
    """Generate C++ code for FixedSoa and VarSoa template specializations.

    This function generates C++ code for:
    - ElementRef/ElementConstRef proxy classes for both FixedSoa and VarSoa
    - SoaTraits specializations in jewels::tap namespace
    - FixedSoa<T, N> template specialization
    - VarSoa<T, max_size> template specialization
    - Non-member equality operators
    - std::swap specializations

    Args:
        compiler_context: The compiler context providing type registry.
        schema_ir: The instantiated schema to generate SoA types for.
        enclosing_namespace: The C++ namespace to generate the code in.

    Returns:
        CppModuleChunks containing the generated header and implementation code.
    """
    verification_max_size: Final = 1
    layout_fixed = tachyon_reg.compute_soa_layout(
        compiler_context, schema_ir, verification_max_size, include_size_field=False
    )
    layout_var = tachyon_reg.compute_soa_layout(
        compiler_context, schema_ir, verification_max_size, include_size_field=True
    )

    cpp_mod = CppModuleChunks()
    schema_cpp_type = typereg.get_cpp_type(compiler_context, schema_ir.as_instantiation_or_resolved_schema())
    schema_name = schema_ir.schema_name

    assert isinstance(schema_cpp_type, (types.CppType, types.CppTemplateType))
    schema_namespace = schema_cpp_type.cpp_namespace or types.GLOBAL_NAMESPACE

    _generate_all_element_ref_classes(cpp_mod, compiler_context, schema_ir, schema_namespace, schema_name)

    fixed_ref_type, fixed_const_ref_type, var_ref_type, var_const_ref_type = _generate_element_ref_types(
        schema_ir, schema_namespace
    )

    _generate_soa_traits_specializations(
        cpp_mod.header_chunk,
        schema_cpp_type,
        fixed_ref_type,
        fixed_const_ref_type,
        var_ref_type,
        var_const_ref_type,
    )

    cpp_mod.header_chunk.append("namespace clockwork")
    cpp_mod.header_chunk.append("{")
    cpp_mod.header_chunk.append("")

    fixed_soa_chunks = _generate_soa_struct(
        compiler_context,
        schema_ir,
        layout_fixed,
        soa_template_name="FixedSoa",
        size_param_name="size",
        include_size_field=False,
        element_ref_type=fixed_ref_type,
        element_const_ref_type=fixed_const_ref_type,
    )
    cpp_mod.append(fixed_soa_chunks)

    var_soa_chunks = _generate_soa_struct(
        compiler_context,
        schema_ir,
        layout_var,
        soa_template_name="VarSoa",
        size_param_name="max_size",
        include_size_field=True,
        element_ref_type=var_ref_type,
        element_const_ref_type=var_const_ref_type,
    )
    cpp_mod.append(var_soa_chunks)

    cpp_mod.header_chunk.append("} // namespace clockwork")
    cpp_mod.header_chunk.append("")

    _add_layout_verification_static_asserts(
        cpp_mod.implementation_chunk, schema_ir, schema_cpp_type, layout_var, enclosing_namespace
    )

    return cpp_mod
