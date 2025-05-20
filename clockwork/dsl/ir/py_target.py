# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CppTarget-related IR nodes."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from typing import TYPE_CHECKING, Final, cast

from clockwork.dsl import cst
from clockwork.dsl.bazel import clk_targets
from clockwork.dsl.bazel.py_targets import PyLibrary
from clockwork.dsl.bazel.targets import Label, get_bazel_label_for_clk_label
from clockwork.dsl.ir import (
    clkbuiltins,
    clkenum,
    expr,
    node,
    primitive,
    schema,
    schema_reg,
    strongtypes,
    typesys,
)
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.interface import InterfaceInstantiation
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.python_cog_dial import PythonCogDial
from clockwork.dsl.ir.representation import ReprInstantiation
from clockwork.dsl.ir.statement import ImmutableBinding
from clockwork.dsl.python import py_context

SUPPORTED_TYPES: Final[list[node.NamedEntity]] = [
    clkbuiltins.BOOL,
    clkbuiltins.BYTE,
    clkbuiltins.FLOAT32,
    clkbuiltins.FLOAT64,
    clkbuiltins.INT64,
    clkbuiltins.INT32,
    clkbuiltins.INT16,
    clkbuiltins.INT8,
    clkbuiltins.UINT64,
    clkbuiltins.UINT32,
    clkbuiltins.UINT16,
    clkbuiltins.UINT8,
]

if TYPE_CHECKING:
    from collections.abc import Mapping


def _render_py_constant(binding: ImmutableBinding) -> str:
    """Render a constant as a python assignment."""
    if binding.value is clkbuiltins.FALSE_VALUE:
        value_str = "False"
    elif binding.value is clkbuiltins.TRUE_VALUE:
        value_str = "True"
    elif isinstance(binding.value, primitive.StringLiteral):
        value_str = f'"{binding.value.value}"'
    elif isinstance(binding.value, primitive.DecimalLiteral):
        value_str = str(binding.value.value)
    else:
        msg = f"Converting {type(binding.value)} to a python value is unsupported."
        raise NotImplementedError(msg)

    doc = f'"""{binding.doc.value}"""\n' if binding.doc else ""
    return f"{doc}{binding.name.upper()}: typing.Final = {value_str}"


@dataclass
class PyTarget(node.NamedEntity, node.DocableEntity, node.CstNode[cst.PyTarget]):
    """IR for PyTargets.

    Need to refactor the py target at some point.  It currently
    depends on InterfaceInstantiation and ReprInstantiation that are
    both tied to c++.  Either need to decouple those from c++ and
    re-use or define new ones specific to py.  However, this was the
    lowest friction route to getting py bindings for now.

    """

    representations: list[ReprInstantiation]
    interfaces: list[InterfaceInstantiation]
    enums: list[EnumTarget]
    constants: dict[str, node.Deferrable[ImmutableBinding]]
    python_cog_dials: list[PythonCogDial]

    @classmethod
    def from_cst(cls: type[PyTarget], cst_node: cst.PyTarget, module: node.Module) -> PyTarget:
        """Create an IR PyTarget from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        representations = []
        interfaces = []
        enums = []
        constants: dict[str, node.Deferrable[ImmutableBinding]] = {}
        python_cog_dials = []
        for statement in cst_node.children_py_target_statement():
            if representation_cst := statement.maybe_cpp_representation():
                representations.append(ReprInstantiation.from_cst(representation_cst, module))
            elif interface_cst := statement.maybe_cpp_interface():
                interface = InterfaceInstantiation.from_cst(interface_cst, module)
                interfaces.append(interface)
            elif enum_cst := statement.maybe_cpp_enum():
                enum = EnumTarget.from_cst(enum_cst, module)
                enums.append(enum)
            elif constant_cst := statement.maybe_target_constant():
                constant_lookup = node.DeferredLookup.make(
                    expected_type=ImmutableBinding,
                    cst_identifier=constant_cst.child_constant_name(),
                    terminals=module.terminals,
                )
                if constant_lookup.identifier in constants:
                    msg = node.append_error_line(
                        constant_cst,
                        module,
                        f"Constant {constant_lookup.identifier} is already included in target {name}",
                    )
                    raise ValueError(msg)
                constants[constant_lookup.identifier] = constant_lookup
            elif python_cog_dial_cst := statement.maybe_py_python_cog_dial():
                python_cog_dial = PythonCogDial.from_cst(python_cog_dial_cst, module)
                python_cog_dials.append(python_cog_dial)
            else:
                msg = node.append_error_line(statement, module, "Unrecognized statement within py_target")
                raise NotImplementedError(msg)

        return cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            name=name,
            scope=module.inner_scope,
            representations=representations,
            interfaces=interfaces,
            enums=enums,
            constants=constants,
            python_cog_dials=python_cog_dials,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for entity in chain(self.representations, self.interfaces, self.enums, self.python_cog_dials):
            # mypy can't/won't reason through chain
            cast("ReprInstantiation | InterfaceInstantiation | PythonCogDial", entity).resolve()

    def _render_constants_interfaces_and_enums(self) -> py_context.PythonChunks:
        python_chunks = py_context.PythonChunks()
        python_chunks.system_imports.add("import pathlib")
        if self.constants.values():
            python_chunks.system_imports.add("import typing")

        python_chunks.imports.add("from clockwork.dsl.ir.module_id import ModuleID")
        python_chunks.imports.add("import clockwork.dsl.ir.compiler")
        python_chunks.imports.add("import clockwork.serialization.py.tachyon_dyn")
        python_chunks.imports.add("import clockwork.dsl.ir.importer")

        python_chunks.impl.append(
            f'_module = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("{self.module.module_id.repo}", pathlib.Path("{self.module.module_id.get_base_path()}")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))'
        )

        for constant in self.constants.values():
            assert isinstance(constant, ImmutableBinding)  # noqa: S101 invariant
            python_chunks.impl.append(_render_py_constant(constant))

        for interface in self.interfaces:
            if not interface.representation:
                msg = f"Unable to locate representation for interface: {interface}.  Is the representation defined in the .clk file?"
                raise ValueError(msg)

            if generic_parameters := interface.representation.schema_ir.schema.generic_parameters():
                repr_info = schema_reg.lookup_representation(self.module.context, interface.representation)
                if repr_info is None:
                    msg = interface.append_error_line("Representation not registered for interface.")
                    raise ValueError(msg)

                resolved_repr = repr_info.representation_ir

                arguments = resolved_repr.get_schema().arguments
                assert arguments is not None  # noqa: S101 invariant

                param_args, module_lookups = _render_generic_parameters(
                    interface,
                    arguments,
                    generic_parameters,
                    self.module.inner_scope,
                )
                python_chunks.impl.extend(module_lookups)

                if not interface.name:
                    msg = interface.append_error_line("Generics in Python require an alias name.")
                    raise ValueError(msg)

                schema_name = interface.representation.schema_ir.schema_name
                name = interface.name
                python_chunks.impl.append(
                    f'{name}, _{name} = clockwork.serialization.py.tachyon_dyn.get_instantiation_dataclass(_module.context, _module, "{schema_name}", {", ".join(param_args)})'
                )

            else:
                schema_name = interface.representation.schema_ir.schema_name
                name = interface.name if interface.name else schema_name
                python_chunks.impl.append(
                    f'{name}, _{name} = clockwork.serialization.py.tachyon_dyn.get_schema_dataclass(_module.context, _module, "{schema_name}")'
                )

        for enum in self.enums:
            if not isinstance(enum.enum_ir, clkenum.ClkEnum):
                msg = "Trying to render an unresolved enum in py_target."
                raise TypeError(msg)
            python_chunks.impl.append(
                f'{enum.enum_ir.name}, _{enum.enum_ir.name} = clockwork.serialization.py.tachyon_dyn.get_enum(_module.context, _module, "{enum.enum_ir.name}")'
            )

        return python_chunks

    def render_to_str(self) -> str:
        """Render to py."""
        python_chunks = py_context.PythonChunks()

        if self.constants.values() or self.interfaces or self.enums:
            python_chunks.append(self._render_constants_interfaces_and_enums())

        for python_cog_dial in self.python_cog_dials:
            python_chunks.append(python_cog_dial.render())

        return python_chunks.render_to_str()

    def render_and_write(self, root_dir: Path) -> None:
        """Convert to Py and write output to files."""
        include_dir = BazelPathResolver().to_buildtime_path(self.module.module_id).parent
        filename = (root_dir / include_dir / self.name).with_suffix(".py")
        with filename.open("w") as f:
            f.write(self.render_to_str())

    def output_targets(self) -> list[PyLibrary]:
        """Extract language target dependency information."""
        dial_targets: set[Label] = {
            get_bazel_label_for_clk_label(self.module.module_id.repo, "//clockwork/dsl/ir:compiler"),
            get_bazel_label_for_clk_label(self.module.module_id.repo, "//clockwork/dsl/ir:importer"),
            get_bazel_label_for_clk_label(self.module.module_id.repo, "//clockwork/serialization/py:tachyon_dyn"),
        }
        for py_dial in self.python_cog_dials:
            dial_targets.update(py_dial.get_bazel_targets())
        return [
            PyLibrary(
                name=self.name,
                srcs=[Path(f"{self.name}.py")],
                deps=sorted(dial_targets),
                data=[
                    clk_targets.module_to_clk(self.module.module_id.repo, self.module.module_id),
                ],
            )
        ]


@dataclass(eq=True, slots=True)
class EnumTarget:
    """Instantiates an enum type inside py_target."""

    enum_ir: clkenum.ClkEnum | expr.Expr

    @classmethod
    def from_cst(
        cls: type[EnumTarget],
        cst_node: cst.CppEnum,
        module: node.Module,
    ) -> EnumTarget:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(enum_ir=typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.enum_ir, expr.Expr):
            msg = f"Attempt to resolve EnumTarget twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (Resolving twice is a runtime error)
        typespec = self.enum_ir.evaluate()
        if not isinstance(typespec, clkenum.ClkEnum):
            msg = self.enum_ir.append_error_line(f"Expected ClkEnum, got {type(typespec)}")
            raise TypeError(msg)
        self.enum_ir = typespec


def _render_generic_parameters(
    interface: InterfaceInstantiation,
    arguments: Mapping[str, typesys.Value],
    generic_parameters: list[typesys.Parameter],
    enclosing_scope: node.Scope,
) -> tuple[list[str], list[str]]:
    """Returns a list of code fragments for parameter lookups and a list of fragments for modules to load."""
    param_args = []
    module_lookups: dict[str, str] = {}
    for param in generic_parameters:
        arg = arguments[param.name]
        if isinstance(param.type_bound, clkbuiltins.IntegerPrimitiveType):
            param_args.append(f"{param.name} = {arg.value_key()}")
        elif param.type_bound == clkbuiltins.TYPE_TYPE:
            lookup_module: node.Module | None = None
            if isinstance(arg, node.NamedEntity) and arg in SUPPORTED_TYPES:
                lookup_name = arg.name
            elif isinstance(arg, strongtypes.StrongType):
                lookup_name, lookup_module = arg.name, arg.module
            elif isinstance(arg, schema.InstantiatedSchema):
                lookup_name, lookup_module = arg.schema_name, arg.schema.module
            else:
                msg = interface.append_error_line(f"Unsupported parameter type: {type(arg)}")
                raise NotImplementedError(msg)

            module_name = "None"
            if lookup_module and lookup_module.inner_scope.uniq_path != enclosing_scope.uniq_path:
                repo_name = lookup_module.module_id.repo
                module_path = lookup_module.module_id.name
                module_name = f"_module_{module_path.replace('::', '_').replace('@', '')}"
                if module_name not in module_lookups:
                    clk_path = Path(module_path.replace("::", "/").replace("@", "")).with_suffix(".clk")
                    module_lookups[module_name] = (
                        f'{module_name} = clockwork.dsl.ir.compiler.compile_source_file(ModuleID.from_path("{repo_name}", pathlib.Path("{clk_path}")), clockwork.dsl.ir.importer.FilesystemImporter(compile_fn=clockwork.dsl.ir.compiler.compile_source_file))'
                    )

            param_args.append(
                f'{param.name} = clockwork.serialization.py.tachyon_dyn.ScopeLookup("{lookup_name}", module={module_name})'
            )

    return param_args, list(module_lookups.values())
