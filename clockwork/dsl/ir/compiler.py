# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for compiling Clockwork source to the IR."""

from collections.abc import Iterable
from dataclasses import dataclass, field
from itertools import chain
from pathlib import Path

from clockwork.dsl.bazel import clk_targets
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import context, typereg, types
from clockwork.dsl.ir import (
    audio,
    box,
    clkbuiltins,
    clkenum,
    cog,
    converter,
    cpp_executable,
    cpp_target,
    extern_type,
    hardware,
    importer,
    nanobind_target,
    node,
    parse,
    policy,
    proto_target,
    pubsub,
    py_target,
    schema,
    schema_reg,
    statement,
    strongtypes,
    system_target,
    typesys,
    udp,
    uuid_reg,
)
from clockwork.dsl.ir.conversion_utils import ConversionRegistration
from clockwork.dsl.ir.interface import InterfaceInstantiation
from clockwork.dsl.ir.module_id import ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver, PathResolver
from clockwork.dsl.ir.representation import (
    Representation,
    RepresentationReference,
    ResolvedReprInstantiation,
)
from clockwork.dsl.proto import proto_typereg
from clockwork.dsl.serialization import tachyon_layout, tachyon_layout_reg, tachyon_reg
from fltk.fegen.pyrt import terminalsrc


def get_resolved_source_text(module_id: ModuleID, path_resolver: PathResolver) -> str:
    """Get the text from the given file, either from runfiles, from bazel-out, or from a straight path lookup."""
    path = path_resolver.find_path(module_id)

    if path:
        return path.read_text()

    msg = f"Couldn't read module {module_id}"
    raise FileNotFoundError(msg)


def compile_source_file(
    module_id: ModuleID, importer: node.Importer, path_resolver: PathResolver | None = None
) -> node.Module:
    """Compiles a DSL source code into a fully-resolved Module IR.

    Args:
        module_id: Module ID to compile.
        importer: An Importer instance responsible for resolving imports.
        path_resolver: Used to convert a module ID to a file path.

    Returns:
        A fully resolved Module instance.
    """
    if not path_resolver:
        path_resolver = BazelPathResolver()

    module = importer.try_cached_load(module_id)
    if module is not None:
        return module

    source_text = get_resolved_source_text(module_id, path_resolver)

    result = compile_source_text(source_text, module_id, importer)
    importer.cache_module(module_id, result)
    return result


def compile_source_text(
    source_text: str,
    module_id: ModuleID,
    importer: node.Importer,
) -> node.Module:
    """Compiles a string containing a module.

    Args:
        source_text: The module contents.
        module_id: The module ID
        importer: An Importer instance responsible for resolving imports.

    Returns:
        A fully resolved Module instance.
    """
    parse_result = parse.clk_string_to_cst(source_text, module_id.get_base_path())

    module = node.Module.from_cst(
        module_id=module_id,
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )

    module.resolve_imports(importer)

    entities = _extract_entities(module, parse_result.terminals)

    _resolve_entities(module, entities)

    return module


def to_clk_target(module_id: ModuleID, search_paths: Iterable[Path] | None = None) -> clk_targets.Clk:
    """Create a Clk target from a clockwork module."""
    source_text = get_resolved_source_text(
        module_id, BazelPathResolver(prefix_paths=list(search_paths) if search_paths else None)
    )
    return to_clk_target_from_text(source_text, module_id, search_paths=search_paths)


def to_clk_target_from_text(
    source_text: str, module_id: ModuleID, search_paths: Iterable[Path] | None = None
) -> clk_targets.Clk:
    """Create a Clk target from a clockwork module."""
    parse_result = parse.clk_string_to_cst(source_text, module_id.get_base_path())
    path_resolver = BazelPathResolver(prefix_paths=list(search_paths) if search_paths else None)
    fs_importer = importer.FilesystemImporter(compile_fn=compile_source_file, path_resolver=path_resolver)

    module = node.Module.from_cst(
        module_id=module_id,
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )

    deps = set()
    for unresolved_import in module.unresolved_imports:
        try:
            # Using statements are ambiguous in clockwork.  Try to
            # resolve the using statement in order to determine the
            # proper clockwork dependency.
            import_spec = fs_importer.resolve_import(module, unresolved_import)
        except FileNotFoundError as err:
            repo_stmt = f"@{unresolved_import.repo}::" if unresolved_import.repo else ""
            use_stmt = f"{repo_stmt}{'::'.join(unresolved_import.path)}"
            msg = node.append_error_line(
                unresolved_import.cst_node,
                module,
                f"Unable to resolve import {use_stmt}.\nPlease manually provide a dependency in the BUILD.bazel file for {module_id}.\nIt's likely this is either a generated file or it comes from an external repository.",
            )
            raise FileNotFoundError(msg) from err
        deps.add(clk_targets.module_to_clk(module_id.repo, import_spec.module_id))

    source_label = clk_targets.module_to_clk(module_id.repo, module_id)
    return clk_targets.Clk(
        name=source_label.name.value,
        srcs=[Path(module_id.get_base_path().name)],
        deps=sorted(deps),
        # Outs are intentionally empty here.  These get populated
        # after compilation in a post processing step.
        outs=[],
    )


@dataclass(slots=True)
class ExtractedEntities:
    """Return structure for _extract_entities."""

    cogs: list[cog.Cog] = field(default_factory=list)
    schemas: list[schema.Schema] = field(default_factory=list)
    enums: list[clkenum.ClkEnum] = field(default_factory=list)
    representations: list[Representation] = field(default_factory=list)
    tags: list[strongtypes.Tag] = field(default_factory=list)
    channels: list[pubsub.Channel] = field(default_factory=list)
    cpp_executables: list[cpp_executable.CppExecutable] = field(default_factory=list)
    cpp_targets: list[cpp_target.CppTarget] = field(default_factory=list)
    nanobind_targets: list[nanobind_target.NanobindTarget] = field(default_factory=list)
    py_targets: list[py_target.PyTarget] = field(default_factory=list)
    proto_targets: list[proto_target.ProtoTarget] = field(default_factory=list)
    boxes: list[box.BoxTemplate] = field(default_factory=list)
    strong_types: list[strongtypes.StrongType] = field(default_factory=list)
    policy_defs: list[policy.PolicyDef] = field(default_factory=list)
    policy_instances: list[policy.PolicyInstance] = field(default_factory=list)
    system_targets: list[system_target.UnresolvedSystemTarget] = field(default_factory=list)
    udp_socket: list[udp.UdpSocket] = field(default_factory=list)
    audio_source: list[audio.AudioSource] = field(default_factory=list)
    extern_types: list[extern_type.ExternType] = field(default_factory=list)
    cpu_domains: list[hardware.CpuDomain] = field(default_factory=list)
    ethernet_lans: list[hardware.EthernetLan] = field(default_factory=list)
    constants: list[statement.ImmutableBinding] = field(default_factory=list)


# We must disable C901 and PLR0912 here (function complexity, branches) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand.
# We could in principle make a data-driven table of handlers instead of explicit
# branches, but it would be awkward and would not decouple the code in a
# meaningful way.
def _extract_entities(  # noqa: C901, PLR0912, PLR0915 (see above)
    module: node.Module,
    terminals: terminalsrc.TerminalSource,
) -> ExtractedEntities:
    """Process Module-scope entities.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().  It extracts all the Module-scope entities from a
    module and produces IR nodes for them.  It also handles triggering name
    resolution after IR creation.
    """
    entities = ExtractedEntities()

    assert module.cst_node is not None  # noqa: S101  (for mypy)
    for entity in module.cst_node.children_entity():
        if cog_cst := entity.maybe_cog():
            cog_ir = cog.Cog.from_cst(module.inner_scope, cog_cst, module=module)
            module.inner_scope.define(cog_ir.name, cog_ir, terminals)
            entities.cogs.append(cog_ir)
        elif schema_cst := entity.maybe_schema():
            schema_ir = schema.Schema.from_cst(
                module=module,
                scope=module.inner_scope,
                cst_schema=schema_cst,
            )
            module.inner_scope.define(schema_ir.name, schema_ir, terminals)
            entities.schemas.append(schema_ir)
        elif enum_cst := entity.maybe_enum():
            enum_ir = clkenum.ClkEnum.from_cst(module=module, scope=module.inner_scope, cst_node=enum_cst)
            entities.enums.append(enum_ir)
        elif tag_cst := entity.maybe_tag():
            tag_ir = strongtypes.Tag.from_cst(cst_node=tag_cst, module=module, scope=module.inner_scope)
            entities.tags.append(tag_ir)
        elif channel_cst := entity.maybe_channel():
            channel_ir = pubsub.Channel.from_cst(cst_node=channel_cst, module=module, scope=module.inner_scope)
            entities.channels.append(channel_ir)
        elif representation_cst := entity.maybe_representation():
            representation_ir = Representation.from_cst(representation_cst, module)
            entities.representations.append(representation_ir)
            if representation_ir.name:
                module.inner_scope.define(representation_ir.name, representation_ir, terminals)
        elif py_target_cst := entity.maybe_py_target():
            py_target_ir = py_target.PyTarget.from_cst(py_target_cst, module)
            module.inner_scope.define(py_target_ir.name, py_target_ir, terminals)
            entities.py_targets.append(py_target_ir)
        elif cpp_target_cst := entity.maybe_cpp_target():
            cpp_target_ir = cpp_target.CppTarget.from_cst(cpp_target_cst, module)
            module.inner_scope.define(cpp_target_ir.name, cpp_target_ir, terminals)
            entities.cpp_targets.append(cpp_target_ir)
        elif nanobind_target_cst := entity.maybe_nanobind_target():
            nanobind_target_ir = nanobind_target.NanobindTarget.from_cst(nanobind_target_cst, module)
            module.inner_scope.define(nanobind_target_ir.name, nanobind_target_ir, terminals)
            entities.nanobind_targets.append(nanobind_target_ir)
        elif box_cst := entity.maybe_box():
            box_ir = box.BoxTemplate.from_cst(box_cst, module)
            module.inner_scope.define(box_ir.name, box_ir, terminals)
            entities.boxes.append(box_ir)
        elif strong_type_cst := entity.maybe_strong_type():
            strong_type_ir = strongtypes.StrongType.from_cst(cst_node=strong_type_cst, module=module)
            module.inner_scope.define(strong_type_ir.name, strong_type_ir, terminals)
            entities.strong_types.append(strong_type_ir)
        elif proto_target_cst := entity.maybe_proto_target():
            proto_target_ir = proto_target.ProtoTarget.from_cst(proto_target_cst, module)
            module.inner_scope.define(proto_target_ir.name, proto_target_ir, terminals)
            entities.proto_targets.append(proto_target_ir)
        elif policy_def_cst := entity.maybe_policy_def():
            policy_def = policy.PolicyDef.from_cst(cst_node=policy_def_cst, module=module)
            module.inner_scope.define(policy_def.name, policy_def, terminals)
            entities.policy_defs.append(policy_def)
        elif policy_inst_cst := entity.maybe_policy():
            policy_inst = policy.PolicyInstance.from_cst(cst_node=policy_inst_cst, module=module)
            entities.policy_instances.append(policy_inst)
        elif udp_socket_cst := entity.maybe_udp_socket():
            udp_socket_ir = udp.UdpSocket.from_cst(udp_socket_cst, module)
            module.inner_scope.define(udp_socket_ir.name, udp_socket_ir, terminals)
            entities.udp_socket.append(udp_socket_ir)
        elif multicast_udp_socket_cst := entity.maybe_multicast_udp_socket():
            udp_socket_ir = udp.UdpSocket.from_cst(multicast_udp_socket_cst, module)
            module.inner_scope.define(udp_socket_ir.name, udp_socket_ir, terminals)
            entities.udp_socket.append(udp_socket_ir)
        elif extern_type_cst := entity.maybe_extern_type():
            extern_type_ir = extern_type.ExternType.from_cst(cst_node=extern_type_cst, module=module)
            module.inner_scope.define(extern_type_ir.name, extern_type_ir, terminals)
            entities.extern_types.append(extern_type_ir)
        elif cpu_domain_cst := entity.maybe_cpu_domain():
            cpu_domain_ir = hardware.CpuDomain.from_cst(cpu_domain_cst, module)
            module.inner_scope.define(cpu_domain_ir.name, cpu_domain_ir, terminals)
            entities.cpu_domains.append(cpu_domain_ir)
        elif ethernet_cst := entity.maybe_ethernet():
            ethernet_lan_ir = hardware.EthernetLan.from_cst(ethernet_cst, module)
            module.inner_scope.define(ethernet_lan_ir.name, ethernet_lan_ir, terminals)
            entities.ethernet_lans.append(ethernet_lan_ir)
        elif cpp_exe_cst := entity.maybe_cpp_executable():
            cpp_exe_ir = cpp_executable.CppExecutable.from_cst(cst_node=cpp_exe_cst, module=module)
            module.inner_scope.define(cpp_exe_ir.name, cpp_exe_ir, terminals)
            entities.cpp_executables.append(cpp_exe_ir)
        elif system_target_cst := entity.maybe_system_target():
            system_target_ir = system_target.UnresolvedSystemTarget.from_cst(system_target_cst, module)
            module.inner_scope.define(system_target_ir.name, system_target_ir, terminals)
            entities.system_targets.append(system_target_ir)
        elif assignment_cst := entity.maybe_assignment_stmt():
            constant_ir = statement.ImmutableBinding.from_cst(assignment_cst, module, module.inner_scope)
            entities.constants.append(constant_ir)
        elif audio_source_cst := entity.maybe_audio_source():
            audio_source_ir = audio.AudioSource.from_cst(audio_source_cst, module)
            module.inner_scope.define(audio_source_ir.name, audio_source_ir, terminals)
            entities.audio_source.append(audio_source_ir)
        else:
            msg = node.append_error_line(entity, module, "Unrecognized module-scope entity")
            raise NotImplementedError(msg)
    for named_entity in chain(
        module.inner_scope.names.values(),
        (r for r in entities.representations if not r.name),
        (policy_instance for policy_instance in entities.policy_instances),
    ):
        resolved = node.resolve_names(named_entity, module.inner_scope)
        if resolved is not named_entity:
            msg = f"Internal error: Module entity identity changed by name resolution: {named_entity} is not {resolved}"
            raise RuntimeError(msg)

    return entities


def _register_entity_uuids(compiler_context: CompilerContext, entities: ExtractedEntities) -> None:
    # Cogs
    for acog in entities.cogs:
        uuid_reg.register_entity(compiler_context, acog, acog.fqn)
        for entity in chain(
            acog.resources.values(),
            acog.configs.values(),
            acog.states.values(),
            acog.inputs.values(),
            acog.outputs.values(),
            acog.conditions.values(),
            acog.diagnostics.values(),
        ):
            uuid_reg.register_entity_with_stable_key(compiler_context, entity)

    # Channels
    for channel in entities.channels:
        if not isinstance(channel, typesys.Value) or not isinstance(channel, node.NamedEntity):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = f"Channel {channel} {type(channel)} is not of the correct type for UUID registration."
            raise TypeError(msg)
        uuid_reg.register_entity(compiler_context, channel, channel.fqn)

    for extern_ir in entities.extern_types:
        uuid_reg.register_entity_with_stable_key(compiler_context, extern_ir)

    for cpu_domain in entities.cpu_domains:
        uuid_reg.register_entity(compiler_context, cpu_domain, cpu_domain.fqn)


def _register_box_instance_uuids(compiler_context: CompilerContext, box_ir: box.Box) -> None:
    uuid_reg.register_entity_with_stable_key(compiler_context, box_ir)
    for instance in box_ir.instances:
        if isinstance(instance, cog.CogInstance):
            uuid_reg.register_entity_with_stable_key(compiler_context, instance)
            for member in instance.members:
                uuid_reg.register_entity_with_stable_key(compiler_context, member)
        elif isinstance(
            instance,
            box.SerializedDataFileInstance | box.StateInstance | box.MemoryResourceInstance | box.ProcessInstance,
        ):
            uuid_reg.register_entity_with_stable_key(compiler_context, instance)
        elif isinstance(instance, udp.UdpSocketInstance):
            uuid_reg.register_entity_with_stable_key(compiler_context, instance)
            for endpoint in (instance.producer_endpoint, instance.observer_endpoint):
                if endpoint:
                    uuid_reg.register_entity_with_stable_key(compiler_context, endpoint)
        elif isinstance(instance, audio.AudioSourceInstance):
            uuid_reg.register_entity_with_stable_key(compiler_context, instance)
            uuid_reg.register_entity_with_stable_key(compiler_context, instance.diagnostics)
        elif isinstance(instance, box.Box):
            _register_box_instance_uuids(compiler_context, instance)
        else:
            msg = box_ir.append_error_line(f"Unrecognized entity {instance.name}: {instance}")
            raise NotImplementedError(msg)


def _resolve_cpp_target_schema_tags(
    schema_tags: list[cpp_target.SchemaTag], namespace: str, module_header: context.Header
) -> None:
    """Resolve SchemaTag(s) in a CppTarget."""
    for schema_tag in schema_tags:
        assert isinstance(schema_tag.schema_ir, schema.Schema)  # noqa: S101  (for mypy)
        if schema_tag.schema_ir.generic_parameters():
            typereg.register_cpp_template(
                schema_tag.schema_ir.module.context,
                schema_tag.schema_ir,
                types.CppTemplate(
                    includes=[module_header],
                    cpp_namespace=namespace,
                    template_name=schema_tag.schema_ir.name,
                ),
            )
        else:
            typereg.register_cpp_type(
                schema_tag.schema_ir.module.context,
                schema_tag.schema_ir,
                types.CppType(
                    includes=[module_header],
                    cpp_namespace=namespace,
                    type_name=schema_tag.schema_ir.name,
                ),
            )


def _resolve_proto_target(proto_target_ir: proto_target.ProtoTarget, module: node.Module) -> None:
    """Resolve a ProtoTarget and process the entities within it.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().
    """
    module_file_name = (module.module_id.get_base_path().parent / proto_target_ir.name).with_suffix(".proto")
    proto_target_ir.resolve()

    for enum in proto_target_ir.enums:
        assert isinstance(enum.enum_ir, clkenum.ClkEnum)  # noqa: S101 (for mypy)
        proto_typereg.register_protobuf_type(
            enum.enum_ir,
            proto_typereg.EnumProtobufType(
                module_id=module.module_id,
                import_location=str(module_file_name),
                package_name=proto_target_ir.options.package,
                type_name=enum.enum_ir.name,
                validate_fields=False,
            ),
        )
    for representation_ir in proto_target_ir.representations:
        resolved_repr = representation_ir.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.schema_ir.schema.parameters:
            args = resolved_repr.typespec.arguments
            if not resolved_repr.name:
                msg = f"Protobuf representations require an alias for instantiated generics: {resolved_repr}"
                raise ValueError(msg)
            proto_typereg.register_protobuf_type(
                args["schema"],
                proto_typereg.DefinedProtobufType(
                    module_id=module.module_id,
                    import_location=str(module_file_name),
                    package_name=proto_target_ir.options.package,
                    type_name=resolved_repr.name,
                    validate_fields=proto_target_ir.options.validate_proto,
                ),
            )
        else:
            proto_typereg.register_protobuf_type(
                resolved_repr.schema_ir,
                proto_typereg.DefinedProtobufType(
                    module_id=module.module_id,
                    import_location=str(module_file_name),
                    package_name=proto_target_ir.options.package,
                    type_name=resolved_repr.name if resolved_repr.name else resolved_repr.schema_ir.schema_name,
                    validate_fields=proto_target_ir.options.validate_proto,
                ),
            )


def _register_representation(representation_ir: ResolvedReprInstantiation) -> None:
    """Register a representation with the schema registry."""
    ref = RepresentationReference.from_typespec(representation_ir.typespec)
    if isinstance(ref, str):
        representation_ir.append_error_line(ref)
        raise TypeError(ref)
    if schema_reg.lookup_representation(representation_ir.module.context, ref) is not None:
        # py_target and cpp_target reuse the same registry.
        return

    try:
        schema_reg.register_representation(
            representation_ir.module.context,
            schema_reg.RepresentationInfo.make(
                representation_ir=representation_ir,
            ),
        )
    except ValueError as exc:
        msg = representation_ir.append_error_line(str(exc))
        raise ValueError(msg).with_traceback(exc.__traceback__) from exc

    uuid_reg.register_entity(
        representation_ir.module.context, representation_ir.typespec, representation_ir.typespec.value_key()
    )


# We must disable C901 here (function complexity) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand.
# We could in principle make a data-driven table of handlers instead of explicit
# branches, but it would be awkward and would not decouple the code in a
# meaningful way.
def _resolve_cpp_target(cpp_target_ir: cpp_target.CppTarget, module: node.Module) -> None:  # noqa: C901 (see above)
    """Resolve a CppTarget and process the entities within it.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().
    """
    cpp_target_header = context.Header(
        module.module_id.repo, (module.module_id.get_base_path().parent / cpp_target_ir.name).with_suffix(".hh")
    )
    cpp_target_ir.resolve()

    _resolve_cpp_target_schema_tags(cpp_target_ir.schema_tags, cpp_target_ir.options.namespace, cpp_target_header)

    for tag in cpp_target_ir.tags:
        assert isinstance(tag.tag_ir, strongtypes.Tag)  # noqa: S101  (for mypy)
        typereg.register_cpp_type(
            module.context,
            tag.tag_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=tag.tag_ir.name,
            ),
        )
    for enum in cpp_target_ir.enums:
        assert isinstance(enum.enum_ir, clkenum.ClkEnum)  # noqa: S101 (for mypy)
        typereg.register_cpp_type(
            module.context,
            enum.enum_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=enum.enum_ir.name,
            ),
        )
    for representation_ir in cpp_target_ir.representations:
        resolved_repr = representation_ir.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.typespec.instantiates is clkbuiltins.TACHYON:
            _process_tachyon_repr(module.context, resolved_repr.typespec)
        elif resolved_repr.typespec.instantiates is clkbuiltins.POD:
            # Special case for POD: It is both representation and interface
            repr_ref = RepresentationReference.from_typespec(resolved_repr.typespec)
            if isinstance(repr_ref, str):
                raise RuntimeError(repr_ref)
            pod_if = InterfaceInstantiation(
                module=module,
                cst_node=None,
                name=resolved_repr.name,
                scope=resolved_repr.scope,
                representation=repr_ref,
                is_generic=resolved_repr.is_generic,
                typespec=resolved_repr.typespec,
            )
            cpp_target_ir.interfaces.append(pod_if)
    for interface in cpp_target_ir.interfaces:
        schema_reg.register_interface(
            cpp_target_ir.module.context,
            schema_reg.InterfaceInfo.make(interface_ir=interface),
        )

    for cpp_cog in cpp_target_ir.cogs:
        assert isinstance(cpp_cog.cog_ir, cog.Cog)  # noqa: S101 (for mypy)
        typereg.register_cpp_type(
            module.context,
            cpp_cog.cog_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=cpp_cog.cog_ir.name + "Factory",
            ),
        )
        cpp_cog.dial_header = context.Header(
            cpp_target_header.repo, cpp_target_header.path.parent / (cpp_target_header.path.stem + "_dial.hh")
        )

    for cpp_udp_socket in cpp_target_ir.udp_sockets:
        assert isinstance(cpp_udp_socket.udp_socket_ir, udp.UdpSocket)  # noqa: S101 (for mypy)
        typereg.register_cpp_type(
            module.context,
            cpp_udp_socket.udp_socket_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=cpp_udp_socket.udp_socket_ir.name,
            ),
        )

    for cpp_audio_source in cpp_target_ir.audio_sources:
        assert isinstance(cpp_audio_source.audio_source_ir, audio.AudioSource)  # noqa: S101 (for mypy)
        typereg.register_cpp_type(
            module.context,
            cpp_audio_source.audio_source_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=cpp_audio_source.audio_source_ir.name,
            ),
        )

    for extern in cpp_target_ir.externs:
        extern.register(cpp_target_header)

    for conv in cpp_target_ir.converters:
        assert isinstance(conv.typespec, typesys.Instantiation)  # noqa: S101 (for mypy)
        converter.register_schema_conversion(
            conv,
            ConversionRegistration(
                parent_target=cpp_target_ir,
                conversion_type=conv.typespec,
                include_location=cpp_target_header,
                namespace=cpp_target_ir.options.namespace,
            ),
        )


def _process_tachyon_repr(compiler_context: CompilerContext, instantiation: typesys.Instantiation) -> None:
    schema_arg = instantiation.arguments["schema"]
    assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)  # noqa: S101  (invariant)
    schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
    layout = tachyon_layout.layout_schema(compiler_context, schema_ir)
    tachyon_layout_reg.register_structured_type(compiler_context, schema_ir, layout)


# We must disable C901 and PLR0912 here (function complexity, branches) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand.
# We could in principle make a data-driven table of handlers instead of explicit
# branches, but it would be awkward and would not decouple the code in a
# meaningful way.
def _resolve_entities(  # noqa: C901, PLR0912 (see above)
    module: node.Module, entities: ExtractedEntities
) -> None:
    for constant_ir in entities.constants:
        constant_ir.resolve()

    for strong_type_ir in entities.strong_types:
        strong_type_ir.resolve()
        _register_strong_type(module.context, strong_type_ir)

    for cpu_domain_ir in entities.cpu_domains:
        cpu_domain_ir.resolve()

    for schema_ir in entities.schemas:
        schema_ir.resolve()

    for cog_ir in entities.cogs:
        cog_ir.resolve()

    for enum_ir in entities.enums:
        enum_ir.resolve()

    for ethernet_lan_ir in entities.ethernet_lans:
        ethernet_lan_ir.resolve()

    for representation_ir in entities.representations:
        representation_ir.resolve()
        if not representation_ir.name:
            # This is a default representation
            schema_reg.register_default_representation_options(module.context, representation_ir)

    for channel_ir in entities.channels:
        channel_ir.resolve()

    for udp_socket_ir in entities.udp_socket:
        udp_socket_ir.resolve()

    for audio_source_ir in entities.audio_source:
        audio_source_ir.resolve()

    for policy_def in entities.policy_defs:
        policy_def.resolve()

    for policy_instance in entities.policy_instances:
        policy_instance.resolve()
        policy.register_policy(module, policy_instance)

    for proto_target_ir in entities.proto_targets:
        _resolve_proto_target(proto_target_ir, module)

    for cpp_exe_ir in entities.cpp_executables:
        cpp_exe_ir.resolve()

    for cpp_target_ir in entities.cpp_targets:
        _resolve_cpp_target(cpp_target_ir, module)

    for nanobind_target_ir in entities.nanobind_targets:
        nanobind_target_ir.resolve()

    for py_target_ir in entities.py_targets:
        py_target_ir.resolve()
        for repr_instantiation_ir in py_target_ir.representations:
            _register_representation(repr_instantiation_ir.get_resolved())

    for system_target_ir in entities.system_targets:
        resolved_system_target = system_target_ir.resolve()
        uuid_reg.register_entity_with_stable_key(module.context, system_target_ir)
        assert resolved_system_target.box_instance.source is not None  # noqa: S101 (invariant)
        _register_box_instance_uuids(module.context, resolved_system_target.box_instance.source)

    _register_entity_uuids(module.context, entities)


def _register_strong_type(compiler_context: CompilerContext, strong_type: strongtypes.StrongType) -> None:
    """Register a strong type with the type registry."""
    if not isinstance(strong_type.typespec, clkbuiltins.PrimitiveType):
        msg = f"Attempt to register unresolved strong type: {strong_type}"
        raise RuntimeError(msg)  # noqa: TRY004  RuntimeError because this is a compiler bug
    constraint = tachyon_reg.constraint_for_type(compiler_context, strong_type.typespec)
    if not constraint:
        msg = f"Constraint not registered for underlying type: {strong_type.typespec}"
        raise ValueError(msg)

    tachyon_reg.register_type(compiler_context, strong_type, constraint)
    cpp_type = typereg.get_cpp_type(strong_type.module.context, strong_type.typespec)
    if not isinstance(cpp_type, types.CppType):
        msg = f"Expected a CppType for underlying type. Received: {cpp_type}"
        raise TypeError(cpp_type)
    typereg.register_cpp_type(strong_type.module.context, strong_type, cpp_type)

    protobuf_type = proto_typereg.get_protobuf_type(strong_type.typespec)
    proto_typereg.register_protobuf_type(strong_type, protobuf_type)
