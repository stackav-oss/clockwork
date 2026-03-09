# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Schema related IR for Proto Targets."""

from __future__ import annotations

from dataclasses import dataclass, field
from itertools import chain
from pathlib import Path
from typing import cast

from clockwork.dsl import clockwork_cst as cst
from clockwork.dsl.bazel import proto_targets
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.composition.str_manip import upper_snake_from_camel
from clockwork.dsl.ir import clkenum, expr, node, schema
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.representation import ReprInstantiation, ResolvedReprInstantiation
from clockwork.dsl.ir.typesys import Instantiation
from clockwork.dsl.serialization import protobuf


def write_to_file(rendered_proto_mod: ProtoModule, root_dir: Path, include_dir: Path, stem: str) -> None:
    """Write proto module to a file.

    Args:
        rendered_proto_mod: The rendered protobuf schecma to write out to a file.
        root_dir: The root directory to write the files to.
        include_dir: The path of the files relative to root_dir.
        stem: The stem for the filename of each output.
    """
    proto_path = (root_dir / include_dir / stem).with_suffix(".proto")
    with proto_path.open("w") as f:
        f.write(rendered_proto_mod.render())


@dataclass(slots=True)
class ProtoGeneratedEntities:
    """Structure for entities passed to ProtoTarget.from_generate_proto."""

    schemas: list[schema.Schema] = field(default_factory=list)
    enums: list[clkenum.ClkEnum] = field(default_factory=list)
    instantiations: list[schema.InstantiateStmt] = field(default_factory=list)


@dataclass
class ProtoTarget(node.NamedEntity, node.DocableEntity, node.CstNode[cst.ProtoTarget]):
    """IR for ProtoTargets."""

    options: ProtoTargetOptions
    representations: list[ReprInstantiation | ResolvedReprInstantiation]
    enums: list[EnumTarget]

    @classmethod
    def from_cst(cls: type[ProtoTarget], cst_node: cst.ProtoTarget, module: node.Module) -> ProtoTarget:
        """Create an IR ProtoTarget from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), terminals=module.terminals)
        options = ProtoTargetOptions.from_cst(cst_node.maybe_proto_target_options(), module, name)
        representations = []
        enums = []
        for statement in cst_node.children_proto_target_statement():
            if representation_cst := statement.maybe_proto_representation():
                representations.append(representation := ReprInstantiation.from_cst(representation_cst, module))
                if representation.is_generic and not representation.name:
                    msg = f"Generic Representations in proto targets require an alias: {representation}"
                    raise ValueError(msg)
                if representation.name:
                    module.inner_scope.define(representation.name, representation, module.terminals)
            elif enum_cst := statement.maybe_proto_enum():
                enums.append(EnumTarget.from_cst(enum_cst, module, options.prefix_enum_value_names))
            else:
                unknown_statement = get_span(statement.span, module.terminals)
                msg = f"ProtoTarget unable to convert statement to IR: '{unknown_statement}'"
                raise ValueError(msg)

        return cls(
            module=module,
            cst_node=cst_node,
            doc=doc,
            name=name,
            scope=module.inner_scope,
            options=options,
            representations=representations,
            enums=enums,
        )

    @classmethod
    def from_generate_proto(
        cls: type[ProtoTarget], module: node.Module, entities: ProtoGeneratedEntities
    ) -> ProtoTarget:
        """Generate an IR ProtoTarget from the entities defined in a module."""
        name = f"{module.module_id.name.split('::')[-1]}_clk_proto"

        assert module.generates is not None
        assert module.inner_attrs is not None

        package = module.inner_attrs.get_proto_package()
        if package is None:
            package = ".".join(
                [
                    module.module_id.repo,
                    *(module.module_id.name.split("::")[:-1]),
                    name,
                ]
            )

        go_package = module.inner_attrs.get_proto_go_package()
        if node.GenerateTarget.go_proto in module.generates and go_package is None:
            msg = node.append_error_line(
                module.cst_node, module, "Generating go_proto but proto go_package attribute is not set"
            )
            raise ValueError(msg)

        options = ProtoTargetOptions(
            module=module,
            cst_node=None,
            package=package,
            go_package=go_package,
            validate_proto=module.inner_attrs.get_proto_validate(),
            prefix_enum_value_names=False,
        )

        enums: list[EnumTarget] = []
        for enum_ir in entities.enums:
            assert enum_ir.attributes
            enums.append(EnumTarget(enum_ir, enum_ir.attributes.get_proto_prefix_enum_value_names()))

        representations: list[ReprInstantiation | ResolvedReprInstantiation] = []

        for instantiation in entities.instantiations:
            assert isinstance(instantiation.typespec, Instantiation)
            assert isinstance(instantiation.typespec.instantiates, schema.Schema)
            representations.append(
                ResolvedReprInstantiation.from_proto_schema(
                    instantiation.typespec,
                    module,
                    instantiation.name or instantiation.typespec.instantiates.name,
                )
            )

        for schema_ir in entities.schemas:
            if schema_ir.programmatically_generated or schema_ir.parameters:
                continue
            representations.append(ResolvedReprInstantiation.from_proto_schema(schema_ir, module, schema_ir.name))

        return cls(
            module=module,
            cst_node=None,
            doc=module.doc,
            name=name,
            scope=module.inner_scope,
            options=options,
            representations=representations,
            enums=enums,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for entity in chain(self.enums, self.representations):
            # mypy can't/won't reason through chain
            cast("EnumTarget | ReprInstantiation", entity).resolve()

    def render_and_write(self, root_dir: Path) -> None:
        """Convert to protobuf and write output to files."""
        proto_module_name = (self.module.module_id.get_base_path().parent / self.name).with_suffix(".proto")
        proto_module = ProtoModule(
            self.options.package,
            self.options.go_package,
            self.options.prefix_enum_value_names,
            messages=[],
            imports=set(),
            enums=self.enums,
            module_name=str(proto_module_name),
        )
        for entity in self.representations:
            if not isinstance(entity.typespec, Instantiation):
                msg = "Representation must be resolved before rendering"
                raise TypeError(msg)
            proto_module.add_message(protobuf.render(entity.name, entity.typespec, entity.module.context))

        write_to_file(
            proto_module, root_dir, BazelPathResolver().to_buildtime_path(self.module.module_id).parent, self.name
        )

    def output_targets(self) -> list[proto_targets.AnyProto]:  # pyright: ignore[reportInvalidTypeForm, reportUnknownParameterType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        """Generate build targets."""
        module_file_name = (self.module.module_id.get_base_path().parent / self.name).with_suffix(".proto")
        path_gen = proto_targets.PathToProto(
            protobuf.ProtobufDep(module_id=self.module.module_id, path=module_file_name)
        )
        deps: set[protobuf.ProtobufDep] = set()
        protobuf_deps: set[protobuf.ProtobufDep] = set()
        go_dep_labels: set[str] = set()
        for entity in self.representations:
            if not isinstance(entity.typespec, Instantiation):
                msg = "Representation must be resolved before rendering"
                raise TypeError(msg)
            proto_layout = protobuf.render(entity.name, entity.typespec, entity.module.context)
            for dep in proto_layout.deps:
                if dep.path.parent == proto_targets.PROTOBUF_PATH:
                    protobuf_deps.add(dep)
                elif dep.path != module_file_name:
                    deps.add(dep)
            go_dep_labels.update(proto_layout.go_dep_labels)
        proto_library_name = path_gen.to_proto_library(path_gen.target).name
        proto_cc_library_name = path_gen.to_proto_cc_library(path_gen.target).name
        proto_py_library_name = path_gen.to_proto_py_library(path_gen.target).name
        proto_go_library_name = path_gen.to_proto_go_library(path_gen.target).name
        proto_compile_name = path_gen.to_proto_compile(path_gen.target).name
        cc_hdr = path_gen.to_cc_hdr(path_gen.target).name
        cc_src = path_gen.to_cc_src(path_gen.target).name
        py = path_gen.to_py(path_gen.target).name
        pyi = path_gen.to_pyi(path_gen.target).name
        go = path_gen.to_go(path_gen.target).name

        outputs = [cc_hdr, cc_src, py, pyi]
        if self.options.go_package is not None:
            outputs.append(go)
        output_mappings = []
        if self.options.go_package is not None:
            output_mappings.append(Label(value=f"{go.name}={self.options.go_package}/{go.name}"))
        plugins = [
            Label(value="@build_stack_rules_proto//plugin/builtin:cpp"),
            Label(value="@build_stack_rules_proto//plugin/builtin:pyi"),
            Label(value="@build_stack_rules_proto//plugin/builtin:python"),
            Label(value="@clockwork//tools/gazelle:protoc-gen-nolint"),
        ]
        if self.options.go_package is not None:
            plugins.append(Label(value="@build_stack_rules_proto//plugin/golang/protobuf:protoc-gen-go"))

        output_targets = [
            proto_targets.ProtoLibrary(
                name=proto_library_name.value,
                srcs=[Path(module_file_name.name)],
                deps=sorted(map(path_gen.to_proto_library, chain(deps, protobuf_deps))),
            ),
            proto_targets.ProtoCompile(
                name=proto_compile_name.value,
                output_mappings=output_mappings,
                outputs=outputs,
                plugins=plugins,
                proto=proto_library_name,
            ),
            proto_targets.ProtoCcLibrary(
                name=proto_cc_library_name.value,
                hdrs=[cc_hdr],
                srcs=[cc_src],
                deps=sorted([Label("@protobuf"), *map(path_gen.to_proto_cc_library, deps)]),
            ),
            proto_targets.ProtoPyLibrary(
                name=proto_py_library_name.value,
                srcs=[py],
                deps=sorted([Label("@protobuf//:protobuf_python"), *map(path_gen.to_proto_py_library, deps)]),
                data=[pyi],
            ),
        ]
        if self.options.go_package is not None:
            output_targets.append(
                proto_targets.ProtoGoLibrary(  # pyright: ignore[reportArgumentType] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy
                    name=proto_go_library_name.value,
                    srcs=[go],
                    deps=sorted(
                        [
                            Label("@org_golang_google_protobuf//reflect/protoreflect"),
                            Label("@org_golang_google_protobuf//runtime/protoimpl"),
                            *map(path_gen.to_proto_go_library, deps),
                            *map(Label, go_dep_labels),
                        ]
                    ),
                    importpath=Label(value=f"{self.options.go_package}"),
                )
            )
        return output_targets


@dataclass
class ProtoTargetOptions(node.CstNode[cst.ProtoTargetOptions]):
    """IR for ProtoTarget options."""

    package: str
    go_package: str | None
    validate_proto: bool
    prefix_enum_value_names: bool

    @classmethod
    def from_cst(
        cls: type[ProtoTargetOptions],
        cst_node: cst.ProtoTargetOptions | None,
        module: node.Module,
        proto_target_name: str,
    ) -> ProtoTargetOptions:
        """Create an IR ProtoTargetOptions from a CST node."""
        package = None
        go_package = None
        validate_proto = None
        prefix_enum_value_names = None

        # parse the options block if it exists
        if cst_node is not None:
            package, go_package, validate_proto, prefix_enum_value_names = ProtoTargetOptions._extract_options(
                cst_node, module
            )

        # default package
        if package is None:
            # Derive default package from module path.
            #
            # For
            # > repo: my_repo
            # > filepath: path/to/my_module.clk
            # > proto_target: my_proto
            #
            # The package will be
            # > my_repo.path.to.my_module.my_proto
            package = ".".join(
                [
                    module.module_id.repo,
                    *module.module_id.name.split("::"),
                    proto_target_name,
                ]
            )

        # default validate_proto
        if validate_proto is None:
            validate_proto = True

        # default validate_proto
        if prefix_enum_value_names is None:
            prefix_enum_value_names = False

        return cls(
            package=package,
            go_package=go_package,
            validate_proto=validate_proto,
            prefix_enum_value_names=prefix_enum_value_names,
            module=module,
            cst_node=cst_node,
        )

    @staticmethod
    def _extract_options(
        cst_node: cst.ProtoTargetOptions,
        module: node.Module,
    ) -> tuple[str | None, str | None, bool | None, bool | None]:
        """Parse the options from the CST node without constructing defaults.

        If an option is not specified, return None.
        """
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        package = None
        go_package = None
        validate_proto = None
        prefix_enum_value_names = None

        for option in cst_node.children_proto_package_option():
            if package is not None:
                msg = node.append_error_line(cst_node, module, "Proto package option specified twice")
                raise ValueError(msg)
            package = get_span(option.child_package_name().span, module.terminals)
        for go_option in cst_node.children_proto_go_package_option():
            if go_package:
                msg = node.append_error_line(cst_node, module, "Go package option specified twice")
                raise ValueError(msg)
            go_package = get_span(go_option.child_go_package_name().span, module.terminals)
        for validation_option in cst_node.children_proto_validation_option():
            if validate_proto:
                msg = node.append_error_line(cst_node, module, "proto validation option specified twice")
                raise ValueError(msg)
            validate_proto = get_span(validation_option.child_boolean().span, module.terminals) == "true"
        for prefix_enum_value_names_option in cst_node.children_proto_prefix_enum_value_names_option():
            if prefix_enum_value_names is not None:
                msg = node.append_error_line(
                    prefix_enum_value_names_option, module, "prefix_enum_value_names option specified twice"
                )
                raise ValueError(msg)
            prefix_enum_value_names = (
                get_span(prefix_enum_value_names_option.child_boolean().span, module.terminals) == "true"
            )

        return package, go_package, validate_proto, prefix_enum_value_names


@dataclass(eq=True, slots=True)
class EnumTarget:
    """Instantiates an enum type inside proto_target."""

    enum_ir: clkenum.ClkEnum | expr.Expr
    prefix_enum_value_names: bool

    @classmethod
    def from_cst(
        cls: type[EnumTarget],
        cst_node: cst.ProtoEnum,
        module: node.Module,
        prefix_enum_value_names: bool,
    ) -> EnumTarget:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(enum_ir=typespec, prefix_enum_value_names=prefix_enum_value_names)

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

    def render(self) -> str:
        """Convert to Protobuf."""
        if not isinstance(self.enum_ir, clkenum.ClkEnum):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        lines = []
        lines.append(f"enum {self.enum_ir.name}" + " {")
        for value in self.enum_ir.values.values():
            assert isinstance(value.integer_value, int)
            assert value.integer_value >= 0
            if self.prefix_enum_value_names:
                lines.append(
                    f"{protobuf.INDENT}{upper_snake_from_camel(self.enum_ir.name + '_' + value.name)} = {value.integer_value};"
                )
            else:
                lines.append(f"{protobuf.INDENT}{value.name} = {value.integer_value};")
        lines.append("}")
        return "\n".join(line for line in lines)


@dataclass
class ProtoModule:
    """Module representing a single proto target."""

    package: str
    go_package: str | None
    prefix_enum_value_names: bool
    messages: list[protobuf.ProtobufMsgLayout]
    enums: list[EnumTarget]
    imports: set[str]
    module_name: str

    def render(self) -> str:
        """Render the module as a string."""
        messages = [message.render() for message in self.messages]
        enums = [enum.render() for enum in self.enums]
        if self.module_name in self.imports:
            self.imports.remove(self.module_name)
        import_list = [_to_import_statement(import_path) for import_path in self.imports]
        import_list.sort()
        lines = list(chain(import_list, enums, messages))
        if self.go_package:
            lines.insert(0, f'option go_package = "{self.go_package}";')
        lines.insert(0, f"package {self.package};")
        lines.insert(0, 'syntax = "proto3";')
        return "\n".join(line for line in lines)

    def add_message(self, proto_message: protobuf.ProtobufMsgLayout) -> None:
        """Add an additional message to the module."""
        self.messages.append(proto_message)
        self.imports.update(proto_message.includes)


def _to_import_statement(import_path: str) -> str:
    return f'import "{import_path}";\n'
