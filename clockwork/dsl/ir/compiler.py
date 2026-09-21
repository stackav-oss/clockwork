# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for compiling Clockwork source to the IR."""

import math
import time
from collections.abc import Iterable
from dataclasses import dataclass, field
from itertools import chain
from pathlib import Path
from typing import final

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.aligner.gen.aligner_cog import make_aligner_cog
from clockwork.dsl.bazel import clk_targets
from clockwork.dsl.cog import clk_cog_metrics
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.cpp import context, types
from clockwork.dsl.cpp import typereg as cpp_typereg
from clockwork.dsl.ir import (
    aligner,
    audio,
    box,
    clkbuiltins,
    clkenum,
    cog,
    cog_components,
    converter,
    cpp_executable,
    cpp_target,
    dfl,
    dfl_types,
    extern_type,
    hardware,
    importer,
    importer_registry,
    nanobind_target,
    node,
    parse,
    policy,
    primitive,
    proto_target,
    pubsub,
    py_target,
    schema,
    schema_reg,
    signal,
    signal_registry,
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
from clockwork.dsl.python import typereg as py_typereg
from clockwork.dsl.serialization import tachyon_layout, tachyon_layout_reg, tachyon_reg
from fltk.fegen.pyrt import terminalsrc


@final
@dataclass(slots=True)
class CompileTiming:
    """Accumulates timing data for one Clockwork compile."""

    parse_elapsed_ns: int = 0
    """Nanoseconds spent parsing source."""

    parsed_module_count: int = 0
    """Number of modules parsed from source text."""

    def record_parse(self, elapsed_ns: int) -> None:
        """Record time spent parsing source text."""
        self.parse_elapsed_ns += elapsed_ns
        self.parsed_module_count += 1


def create_filesystem_importer() -> node.Importer:
    """Create an instance of the FilesystemImporter with the compile_fn property set."""
    return importer.FilesystemImporter(compile_fn=compile_source_file)


def _store_importer_in_context(compiler_context: CompilerContext, importer_instance: node.Importer) -> None:
    """Store an importer in the compiler context.

    Args:
        compiler_context: The compiler context to store the importer in.
        importer_instance: The importer instance to store.

    Raises:
        RuntimeError: If the context already has a different importer instance.
    """
    registry = compiler_context[importer_registry.IMPORTER_REGISTRY_KEY]
    if registry.importer is None:
        registry.importer = importer_instance
    elif registry.importer is not importer_instance:
        msg = f"Attempt to use different importer instances in the same context: {registry.importer} vs {importer_instance}"
        raise RuntimeError(msg)


def get_resolved_source_text(module_id: ModuleID, path_resolver: PathResolver) -> str:
    """Get the text from the given file, either from runfiles, from bazel-out, or from a straight path lookup."""
    path = path_resolver.find_path(module_id)

    if path:
        return path.read_text()

    msg = f"Couldn't read module {module_id}"
    raise FileNotFoundError(msg)


def compile_source_file(
    module_id: ModuleID,
    importer: node.Importer,
    path_resolver: PathResolver | None = None,
    timing: CompileTiming | None = None,
) -> node.Module:
    """Compiles a DSL source code into a fully-resolved Module IR.

    Args:
        module_id: Module ID to compile.
        importer: An Importer instance responsible for resolving imports.
        path_resolver: Used to convert a module ID to a file path.
        timing: An optional accumulator for parse timing.

    Returns:
        A fully resolved Module instance.
    """
    if not path_resolver:
        path_resolver = BazelPathResolver()

    module = importer.try_cached_load(module_id)
    if module is not None:
        return module

    source_text = get_resolved_source_text(module_id, path_resolver)
    module = compile_source_text(source_text, module_id, importer, timing=timing)

    importer.cache_module(module_id, module)

    return module


def resolve_module(
    module: node.Module,
    importer: node.Importer,
    terminals: terminalsrc.TerminalSource,
) -> None:
    """Resolve a module.

    Args:
        module: The module ID
        importer: An Importer instance responsible for resolving imports.
        terminals: Terminal source.
    """
    _handle_cog_imports(module)
    module.resolve_imports(importer)

    entities = _extract_entities(module, terminals)

    _resolve_entities(module, entities)
    _generate_targets(module, entities, terminals)

    # Aligners are resolved after target generation so that interfaces are
    # registered (by _generate_targets / _resolve_entities) before
    # resolve_schema_interface is called.
    for aligner_ir in entities.aligners:
        aligner_ir.resolve()
    has_generate_cpp_aligner = module.generates is not None and node.GenerateTarget.cpp_aligner in module.generates
    has_manual_cpp_targets = bool(entities.cpp_targets)

    if has_generate_cpp_aligner:
        if not entities.aligners:
            msg = "Module has cpp_aligner generation target but no aligner defined"
            raise ValueError(msg)
        _generate_aligner_targets(module, entities, terminals)
    if has_manual_cpp_targets and entities.aligners:
        _populate_manual_aligner_cogs(module, entities)

    for cog_ir in entities.cogs:
        if cog_ir.aligned_inputs:
            _expand_aligned_input_class_defs(cog_ir)
            for input_def in cog_ir.expanded_aligned_input_defs.values():
                uuid_reg.register_entity_with_stable_key(module.context, input_def)


def compile_source_text(
    source_text: str,
    module_id: ModuleID,
    importer: node.Importer,
    timing: CompileTiming | None = None,
) -> node.Module:
    """Compiles a string containing a module.

    Args:
        source_text: The module contents.
        module_id: The module ID
        importer: An Importer instance responsible for resolving imports.
        timing: An optional accumulator for parse timing.

    Returns:
        A fully resolved Module instance.
    """
    start_ns = time.perf_counter_ns()
    parse_result = parse.clk_string_to_cst(source_text, module_id.get_base_path())
    if timing is not None:
        timing.record_parse(time.perf_counter_ns() - start_ns)

    module = node.Module.from_cst(
        module_id=module_id,
        builtins=clkbuiltins.BUILTINS_SCOPE,
        cst_node=parse_result.cst,
        terminals=parse_result.terminals,
    )

    _store_importer_in_context(module.context, importer)
    resolve_module(module, importer, parse_result.terminals)

    return module


def to_clk_target(module_id: ModuleID, search_paths: Iterable[Path] | None = None) -> clk_targets.Clk:
    """Create a Clk target from a clockwork module."""
    source_text = get_resolved_source_text(
        module_id, BazelPathResolver(prefix_paths=list(search_paths) if search_paths else None)
    )
    return to_clk_target_from_text(source_text, module_id, search_paths=search_paths)


def _handle_cog_metrics_imports(module: node.Module, cst_cog: cst.Cog | cst.PythonCog) -> bool:
    metrics_enabled = False
    if options_cst := cst_cog.child_cog_blocks().maybe_metrics_options_block():
        options = cog.MetricsOptions.from_cst(options_cst, module)
        # Presence of a metrics_options block implies legacy metrics opt-in.
        # Default to enabled unless explicitly disabled.
        metrics_enabled = options.metrics_enabled
    if metrics_enabled:
        module.unresolved_imports.extend(clk_cog_metrics.get_cog_metrics_imports(module))
        return True
    return False


def _handle_cog_imports(module: node.Module) -> None:
    assert module.cst_node is not None
    handled = False
    for entity in module.cst_node.children_entity():
        if handled:
            break
        if cst_cog := entity.maybe_cog():
            handled = _handle_cog_metrics_imports(module, cst_cog)
    for entity in module.cst_node.children_clk_entity():
        if handled:
            break
        if cst_cog := entity.maybe_cog() or entity.maybe_python_cog():
            handled = _handle_cog_metrics_imports(module, cst_cog)


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
    _store_importer_in_context(module.context, fs_importer)
    _handle_cog_imports(module)
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
    signals: list[signal.Signal] = field(default_factory=list)
    cpp_executables: list[cpp_executable.CppExecutable] = field(default_factory=list)
    cpp_targets: list[cpp_target.CppTarget] = field(default_factory=list)
    nanobind_targets: list[nanobind_target.NanobindTarget] = field(default_factory=list)
    py_targets: list[py_target.PyTarget] = field(default_factory=list)
    proto_targets: list[proto_target.ProtoTarget] = field(default_factory=list)
    boxes: list[box.BoxTemplate] = field(default_factory=list)
    box_instantiations: list[statement.InstantiateStmt] = field(default_factory=list)
    strong_types: list[strongtypes.StrongType] = field(default_factory=list)
    policy_defs: list[policy.PolicyDef] = field(default_factory=list)
    policy_instances: list[policy.PolicyInstance] = field(default_factory=list)
    system_targets: list[system_target.UnresolvedSystemTarget] = field(default_factory=list)
    udp_socket: list[udp.UdpSocket] = field(default_factory=list)
    audio_source: list[audio.AudioSource] = field(default_factory=list)
    extern_types: list[extern_type.ExternType] = field(default_factory=list)
    cpu_domains: list[hardware.CpuDomain] = field(default_factory=list)
    pcie_links: list[hardware.PcieLink] = field(default_factory=list)
    ethernet_lans: list[hardware.EthernetLan] = field(default_factory=list)
    constants: list[statement.ImmutableBinding] = field(default_factory=list)
    instantiations: list[statement.InstantiateStmt] = field(default_factory=list)
    cog_instantiations: list[statement.InstantiateStmt] = field(default_factory=list)
    schema_instantiations: list[statement.InstantiateStmt] = field(default_factory=list)
    dfl_trait_defs: list[dfl_types.TraitDef] = field(default_factory=list)
    dfl_trait_impls: list[dfl_types.TraitImpl] = field(default_factory=list)
    dfl_fn_defs: list[dfl.FnDef] = field(default_factory=list)
    aligners: list[aligner.Aligner] = field(default_factory=list)


_UNDEFINED_ID_PREFIX = "Undefined identifier "


def _maybe_raise_aligner_schema_hint(exc: ValueError, pending_names: set[str]) -> None:
    """Re-raise with guidance if the undefined identifier is an aligner-generated schema.

    When an aligner and a cog that references its generated alignment schema
    live in the same ``.clk`` file, the schema name does not exist yet at name
    resolution time (it is created later during aligner codegen).  Detect this
    case and surface a clear message so the user knows to split the aligner
    into a separate file.
    """
    text = str(exc)
    if not text.startswith(_UNDEFINED_ID_PREFIX):
        return
    # The identifier sits between the prefix and the first newline (which
    # begins the source-location annotation added by DeferredLookup.resolve).
    identifier = text[len(_UNDEFINED_ID_PREFIX) :].split("\n", 1)[0]
    if identifier not in pending_names:
        return
    aligner_name = identifier.removesuffix("AlignmentMsg")
    msg = (
        f"{text}\n\n"
        + f"Hint: '{identifier}' is the alignment schema that will be generated for "
        + f"aligner '{aligner_name}', but it is not available during name resolution "
        + "because the schema is created after module evaluation.\n"
        + "Move the aligner definition into a separate .clk file and import it."
    )
    raise ValueError(msg) from None


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

    assert module.cst_node is not None

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
        elif signal_cst := entity.maybe_module_scope_signal():
            signal_ir = signal.Signal.from_cst(cst_node=signal_cst, module=module, scope=module.inner_scope)
            entities.signals.append(signal_ir)
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
        elif pcie_link_cst := entity.maybe_pcie_link():
            pcie_link_ir = hardware.PcieLink.from_cst(pcie_link_cst, module)
            entities.pcie_links.append(pcie_link_ir)
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
        elif box_cst := entity.maybe_box():
            box_ir = box.BoxTemplate.from_cst(box_cst, module)
            module.inner_scope.define(box_ir.name, box_ir, terminals)
            entities.boxes.append(box_ir)
        elif trait_def_cst := entity.maybe_dfl_trait_def():
            trait_def_ir = dfl_types.TraitDef.from_cst(trait_def_cst, module, module.inner_scope)
            module.inner_scope.define(trait_def_ir.name, trait_def_ir, terminals)
            entities.dfl_trait_defs.append(trait_def_ir)
        elif trait_impl_cst := entity.maybe_dfl_impl_decl():
            trait_impl_ir = dfl_types.TraitImpl.from_cst(trait_impl_cst, module)
            entities.dfl_trait_impls.append(trait_impl_ir)
        elif fn_def_cst := entity.maybe_dfl_fn_def():
            fn_def_ir = dfl.FnDef.from_cst(fn_def_cst, module, module.inner_scope)
            module.inner_scope.define(fn_def_ir.name, fn_def_ir, terminals)
            entities.dfl_fn_defs.append(fn_def_ir)
        elif aligner_cst := entity.maybe_aligner():
            aligner_ir = aligner.Aligner.from_cst(aligner_cst, module, module.inner_scope)
            module.inner_scope.define(aligner_ir.name, aligner_ir, terminals)
            entities.aligners.append(aligner_ir)
        else:
            msg = node.append_error_line(entity, module, "Unrecognized module-scope entity")
            raise NotImplementedError(msg)

    for entity in module.cst_node.children_clk_entity():
        if assignment_cst := entity.maybe_assignment_stmt():
            constant_ir = statement.ImmutableBinding.from_cst(assignment_cst, module, module.inner_scope)
            entities.constants.append(constant_ir)
        elif audio_source_cst := entity.maybe_audio_source():
            audio_source_ir = audio.AudioSource.from_cst(audio_source_cst, module)
            module.inner_scope.define(audio_source_ir.name, audio_source_ir, terminals)
            entities.audio_source.append(audio_source_ir)
        elif box_cst := entity.maybe_box():
            box_ir = box.BoxTemplate.from_cst(box_cst, module)
            module.inner_scope.define(box_ir.name, box_ir, terminals)
            entities.boxes.append(box_ir)
        elif channel_cst := entity.maybe_channel():
            channel_ir = pubsub.Channel.from_cst(cst_node=channel_cst, module=module, scope=module.inner_scope)
            entities.channels.append(channel_ir)
        elif cog_cst := entity.maybe_cog():
            cog_ir = cog.Cog.from_cst(module.inner_scope, cog_cst, module=module)
            module.inner_scope.define(cog_ir.name, cog_ir, terminals)
            entities.cogs.append(cog_ir)
        elif cpu_domain_cst := entity.maybe_cpu_domain():
            cpu_domain_ir = hardware.CpuDomain.from_cst(cpu_domain_cst, module)
            module.inner_scope.define(cpu_domain_ir.name, cpu_domain_ir, terminals)
            entities.cpu_domains.append(cpu_domain_ir)
        elif enum_cst := entity.maybe_enum():
            enum_ir = clkenum.ClkEnum.from_cst(module=module, scope=module.inner_scope, cst_node=enum_cst)
            entities.enums.append(enum_ir)
        elif ethernet_cst := entity.maybe_ethernet():
            ethernet_lan_ir = hardware.EthernetLan.from_cst(ethernet_cst, module)
            module.inner_scope.define(ethernet_lan_ir.name, ethernet_lan_ir, terminals)
            entities.ethernet_lans.append(ethernet_lan_ir)
        elif extern_type_cst := entity.maybe_extern_type():
            extern_type_ir = extern_type.ExternType.from_cst(cst_node=extern_type_cst, module=module)
            module.inner_scope.define(extern_type_ir.name, extern_type_ir, terminals)
            entities.extern_types.append(extern_type_ir)
        elif instantiation_cst := entity.maybe_clk_instantiate_stmt():
            instantiation_ir = statement.InstantiateStmt.from_cst(instantiation_cst, module)
            if instantiation_ir.name:
                module.inner_scope.define(instantiation_ir.name, instantiation_ir, terminals)
            entities.instantiations.append(instantiation_ir)
        elif multicast_udp_socket_cst := entity.maybe_multicast_udp_socket():
            udp_socket_ir = udp.UdpSocket.from_cst(multicast_udp_socket_cst, module)
            module.inner_scope.define(udp_socket_ir.name, udp_socket_ir, terminals)
            entities.udp_socket.append(udp_socket_ir)
        elif pcie_link_cst := entity.maybe_pcie_link():
            pcie_link_ir = hardware.PcieLink.from_cst(pcie_link_cst, module)
            entities.pcie_links.append(pcie_link_ir)
        elif policy_def_cst := entity.maybe_policy_def():
            policy_def = policy.PolicyDef.from_cst(cst_node=policy_def_cst, module=module)
            module.inner_scope.define(policy_def.name, policy_def, terminals)
            entities.policy_defs.append(policy_def)
        elif policy_inst_cst := entity.maybe_policy():
            policy_inst = policy.PolicyInstance.from_cst(cst_node=policy_inst_cst, module=module)
            entities.policy_instances.append(policy_inst)
        elif python_cog_cst := entity.maybe_python_cog():
            python_cog_ir = cog.Cog.from_cst(module.inner_scope, python_cog_cst, module=module)
            module.inner_scope.define(python_cog_ir.name, python_cog_ir, terminals)
            entities.cogs.append(python_cog_ir)
        elif schema_cst := entity.maybe_schema():
            schema_ir = schema.Schema.from_cst(
                module=module,
                scope=module.inner_scope,
                cst_schema=schema_cst,
            )
            module.inner_scope.define(schema_ir.name, schema_ir, terminals)
            entities.schemas.append(schema_ir)
        elif signal_cst := entity.maybe_module_scope_signal():
            signal_ir = signal.Signal.from_cst(cst_node=signal_cst, module=module, scope=module.inner_scope)
            entities.signals.append(signal_ir)
        elif strong_type_cst := entity.maybe_strong_type():
            strong_type_ir = strongtypes.StrongType.from_cst(cst_node=strong_type_cst, module=module)
            module.inner_scope.define(strong_type_ir.name, strong_type_ir, terminals)
            entities.strong_types.append(strong_type_ir)
        elif tag_cst := entity.maybe_tag():
            tag_ir = strongtypes.Tag.from_cst(cst_node=tag_cst, module=module, scope=module.inner_scope)
            entities.tags.append(tag_ir)
        elif udp_socket_cst := entity.maybe_udp_socket():
            udp_socket_ir = udp.UdpSocket.from_cst(udp_socket_cst, module)
            module.inner_scope.define(udp_socket_ir.name, udp_socket_ir, terminals)
            entities.udp_socket.append(udp_socket_ir)
        elif trait_def_cst := entity.maybe_dfl_trait_def():
            trait_def_ir = dfl_types.TraitDef.from_cst(trait_def_cst, module, module.inner_scope)
            module.inner_scope.define(trait_def_ir.name, trait_def_ir, terminals)
            entities.dfl_trait_defs.append(trait_def_ir)
        elif trait_impl_cst := entity.maybe_dfl_impl_decl():
            trait_impl_ir = dfl_types.TraitImpl.from_cst(trait_impl_cst, module)
            entities.dfl_trait_impls.append(trait_impl_ir)
        elif fn_def_cst := entity.maybe_dfl_fn_def():
            fn_def_ir = dfl.FnDef.from_cst(fn_def_cst, module, module.inner_scope)
            module.inner_scope.define(fn_def_ir.name, fn_def_ir, terminals)
            entities.dfl_fn_defs.append(fn_def_ir)
        elif aligner_cst := entity.maybe_aligner():
            aligner_ir = aligner.Aligner.from_cst(aligner_cst, module, module.inner_scope)
            module.inner_scope.define(aligner_ir.name, aligner_ir, terminals)
            entities.aligners.append(aligner_ir)
        else:
            msg = node.append_error_line(entity, module, "Unrecognized module-scope entity")
            raise NotImplementedError(msg)

    pending_aligner_schema_names = {f"{a.name}AlignmentMsg" for a in entities.aligners}

    for named_entity in chain(
        module.inner_scope.names.values(),
        (r for r in entities.representations if not r.name),
        (policy_instance for policy_instance in entities.policy_instances),
        (trait_impl for trait_impl in entities.dfl_trait_impls),
        (instantiate for instantiate in entities.instantiations),
    ):
        try:
            resolved = node.resolve_names(named_entity, module.inner_scope)
        except ValueError as exc:
            _maybe_raise_aligner_schema_hint(exc, pending_aligner_schema_names)
            raise
        if resolved is not named_entity:
            msg = f"Internal error: Module entity identity changed by name resolution: {named_entity} is not {resolved}"
            raise RuntimeError(msg)

    return entities


# We must disable C901 here (function complexity) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand
def _register_entity_uuids(compiler_context: CompilerContext, entities: ExtractedEntities) -> None:  # noqa: C901 (See above)
    # Cogs
    for acog in entities.cogs:
        uuid_reg.register_entity(compiler_context, acog, acog.fqn)
        for entity in chain(
            acog.resources.values(),
            acog.configs.values(),
            acog.states.values(),
            acog.inputs.values(),
            acog.aligned_inputs.values(),
            acog.outputs.values(),
            acog.metrics_outputs.values(),
            acog.report_groups.values(),
            acog.cog_metrics_report_groups.values(),
            acog.conditions.values(),
            acog.diagnostics.values(),
        ):
            if isinstance(entity, cog.InputDef) and entity.elements:
                for element in entity.elements:
                    uuid_reg.register_entity_with_stable_key(compiler_context, element)
            uuid_reg.register_entity_with_stable_key(compiler_context, entity)

    # InstantiatedCogs
    for inst in entities.cog_instantiations:
        assert isinstance(inst.instantiated, typesys.Value)
        uuid_reg.register_entity(compiler_context, inst.instantiated, inst.instantiated.fqn)

    # Channels
    for channel in entities.channels:
        if not isinstance(channel, typesys.Value) or not isinstance(channel, node.NamedEntity):  # pyright: ignore[reportUnnecessaryIsInstance] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
            msg = f"Channel {channel} {type(channel)} is not of the correct type for UUID registration."
            raise TypeError(msg)
        if not channel.generic_parameters():
            uuid_reg.register_entity(compiler_context, channel, channel.fqn)

    for extern_ir in entities.extern_types:
        uuid_reg.register_entity_with_stable_key(compiler_context, extern_ir)

    for cpu_domain in entities.cpu_domains:
        uuid_reg.register_entity(compiler_context, cpu_domain, cpu_domain.fqn)


# We must disable C901 here (function complexity) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand
def _register_box_instance_uuids(compiler_context: CompilerContext, box_ir: box.Box) -> None:  # noqa: C901 (see above)
    uuid_reg.register_entity_with_stable_key(compiler_context, box_ir)
    for instance in box_ir.instances:
        if isinstance(instance, cog.CogInstance):
            uuid_reg.register_entity_with_stable_key(compiler_context, instance)
            for member in instance.members:
                uuid_reg.register_entity_with_stable_key(compiler_context, member)
                if isinstance(member.member, cog.InputDef) and member.elements is not None:
                    for element in member.elements:
                        uuid_reg.register_entity_with_stable_key(compiler_context, element)
        elif isinstance(
            instance,
            box.FirstMessageInstance
            | box.SerializedDataFileInstance
            | box.StateInstance
            | box.MemoryResourceInstance
            | box.ProcessInstance,
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


def _register_cpp_target_schema_tags(
    cpp_target_ir: cpp_target.CppTarget, module: node.Module, module_header: context.Header
) -> None:
    """Register SchemaTag(s) in a CppTarget.

    The retry_errors flag is initially True so that we try to register any failed schema tags
    again after attempting to register the remaining unregistered schema tags. If we go through
    the loop and are unable to register any schema tags we set retry_errors to False so that the user
    sees the error that is preventing registration to make progress.
    """
    registered_schema_tags = []
    pending_schema_tags = cpp_target_ir.schema_tags
    retry_errors = True
    while pending_schema_tags:
        failed_schema_tags = []
        for schema_tag in pending_schema_tags:
            assert isinstance(schema_tag.schema_ir, schema.Schema)
            generic_parameters = schema_tag.schema_ir.generic_parameters()
            if generic_parameters:
                try:
                    for param in generic_parameters:
                        if isinstance(param.default, typesys.TypeVal):
                            _ = cpp_typereg.get_cpp_type(module.context, param.default)
                except TypeError:
                    if not retry_errors:
                        raise
                    failed_schema_tags.append(schema_tag)
                    continue
                cpp_typereg.register_cpp_template(
                    module.context,
                    schema_tag.schema_ir,
                    types.CppTemplate(
                        includes=[module_header],
                        cpp_namespace=cpp_target_ir.options.namespace,
                        template_name=schema_tag.schema_ir.name,
                    ),
                )
            else:
                # Programmatically-generated schemas (e.g. aligner alignment
                # output schemas) may be defined in the same CppTarget as the
                # cog that publishes them. The cog's dial header references the
                # schema type but is #included before the struct definition in
                # the main .hh. Using a forward declaration instead of the full
                # Header avoids a circular include (dial → main → dial) while
                # still making the type visible when the dial header is expanded.
                # External consumers get the full definition from the target's
                # main .hh which they include transitively.
                if schema_tag.schema_ir.programmatically_generated:
                    schema_includes: list[context.Include] = [
                        context.FwdDecl(
                            cpp_target_ir.options.namespace,
                            f"struct {schema_tag.schema_ir.name}",
                        )
                    ]
                else:
                    schema_includes = [module_header]
                cpp_typereg.register_cpp_type(
                    module.context,
                    schema_tag.schema_ir,
                    types.CppType(
                        includes=schema_includes,
                        cpp_namespace=cpp_target_ir.options.namespace,
                        type_name=schema_tag.schema_ir.name,
                    ),
                )
            registered_schema_tags.append(schema_tag)
        if len(failed_schema_tags) == len(pending_schema_tags):
            retry_errors = False
        pending_schema_tags = failed_schema_tags
    cpp_target_ir.schema_tags = registered_schema_tags


def _register_proto_target(proto_target_ir: proto_target.ProtoTarget, module: node.Module) -> None:
    """Resolve a ProtoTarget and process the entities within it.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().
    """
    module_file_name = (module.module_id.get_base_path().parent / proto_target_ir.name).with_suffix(".proto")

    for enum in proto_target_ir.enums:
        assert isinstance(enum.enum_ir, clkenum.ClkEnum)
        proto_typereg.register_protobuf_type(
            enum.enum_ir,
            proto_typereg.EnumProtobufType(
                module_id=module.module_id,
                import_location=str(module_file_name),
                package_name=proto_target_ir.options.package,
                type_name=enum.enum_ir.name,
                go_dep_label=None,
                validate_fields=False,
            ),
            module.context,
        )
    for representation_ir in proto_target_ir.representations:
        resolved_repr = representation_ir.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.schema_ir.schema.parameters:
            args = resolved_repr.typespec.arguments
            if not resolved_repr.name:
                msg = f"Protobuf representations require an alias for instantiated generics: {resolved_repr}"
                raise ValueError(msg)
            schema_arg = args["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
            proto_typereg.register_protobuf_type(
                schema_ir,
                proto_typereg.DefinedProtobufType(
                    module_id=module.module_id,
                    import_location=str(module_file_name),
                    package_name=proto_target_ir.options.package,
                    type_name=resolved_repr.name,
                    go_dep_label=None,
                    validate_fields=proto_target_ir.options.validate_proto,
                ),
                module.context,
            )
        else:
            proto_typereg.register_protobuf_type(
                resolved_repr.schema_ir,
                proto_typereg.DefinedProtobufType(
                    module_id=module.module_id,
                    import_location=str(module_file_name),
                    package_name=proto_target_ir.options.package,
                    type_name=resolved_repr.name or resolved_repr.schema_ir.schema_name,
                    go_dep_label=None,
                    validate_fields=proto_target_ir.options.validate_proto,
                ),
                module.context,
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


# We must disable C901 and PLR0912 here (function complexity, branches) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand.
# We could in principle make a data-driven table of handlers instead of explicit
# branches, but it would be awkward and would not decouple the code in a
# meaningful way.
def _register_cpp_target(cpp_target_ir: cpp_target.CppTarget, module: node.Module) -> None:  # noqa: C901, PLR0912 (See above).
    """Register the entities within a CppTarget.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().
    """
    cpp_target_header = context.Header(
        module.module_id.repo, (module.module_id.get_base_path().parent / cpp_target_ir.name).with_suffix(".hh")
    )

    assert cpp_target_ir.options is not None
    _register_cpp_target_schema_tags(cpp_target_ir, module, cpp_target_header)

    for tag in cpp_target_ir.tags:
        assert isinstance(tag.tag_ir, strongtypes.Tag)
        cpp_typereg.register_cpp_type(
            module.context,
            tag.tag_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=tag.tag_ir.name,
            ),
        )
    for enum in cpp_target_ir.enums:
        assert isinstance(enum.enum_ir, clkenum.ClkEnum)
        cpp_typereg.register_cpp_type(
            module.context,
            enum.enum_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=enum.enum_ir.name,
            ),
        )

    _register_cpp_target_representations(cpp_target_ir, module)
    _register_cpp_target_representations_and_interfaces(cpp_target_ir, module)

    for interface in cpp_target_ir.interfaces:
        schema_reg.register_interface(
            cpp_target_ir.module.context,
            schema_reg.InterfaceInfo.make(interface_ir=interface),
        )
    for _, interface in cpp_target_ir.representations_and_interfaces:
        schema_reg.register_interface(
            cpp_target_ir.module.context,
            schema_reg.InterfaceInfo.make(interface_ir=interface),
        )

    for instantiate_stmt in cpp_target_ir.instantiations:
        assert isinstance(instantiate_stmt.instantiated, cog.InstantiatedCog)
        cog.register_instantiated_cog(module.context, instantiate_stmt.instantiated)

    for cpp_cog in cpp_target_ir.cogs:
        assert isinstance(cpp_cog.cog_ir, cog.Cog)
        if cpp_cog.cog_ir.is_generic():
            cpp_typereg.register_cpp_template(
                module.context,
                cpp_cog.cog_ir,
                types.CppTemplate(
                    includes=[cpp_target_header],
                    cpp_namespace=cpp_target_ir.options.namespace,
                    template_name=cpp_cog.cog_ir.name + "Factory",
                ),
            )
        else:
            cpp_typereg.register_cpp_type(
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
        cpp_cog.cog_header = cpp_target_header

    for inst in cpp_target_ir.instantiations:
        assert isinstance(inst.instantiated, typesys.Value)
        uuid_reg.register_entity(module.context, inst.instantiated, inst.instantiated.fqn)

    for cpp_instantiated_cog in cpp_target_ir.instantiated_cogs:
        assert isinstance(cpp_instantiated_cog.instantiation, cog.InstantiatedCog)
        cpp_instantiated_cog.dial_header = context.Header(
            cpp_target_header.repo, cpp_target_header.path.parent / (cpp_target_header.path.stem + "_dial.hh")
        )
        cpp_instantiated_cog.cog_header = cpp_target_header

    _register_cpp_target_dials(cpp_target_ir, module)

    for cpp_udp_socket in cpp_target_ir.udp_sockets:
        assert isinstance(cpp_udp_socket.udp_socket_ir, udp.UdpSocket)
        cpp_typereg.register_cpp_type(
            module.context,
            cpp_udp_socket.udp_socket_ir,
            types.CppType(
                includes=[cpp_target_header],
                cpp_namespace=cpp_target_ir.options.namespace,
                type_name=cpp_udp_socket.udp_socket_ir.name,
            ),
        )

    for cpp_audio_source in cpp_target_ir.audio_sources:
        assert isinstance(cpp_audio_source.audio_source_ir, audio.AudioSource)
        cpp_typereg.register_cpp_type(
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
        assert isinstance(conv.typespec, typesys.Instantiation)
        converter.register_schema_conversion(
            conv,
            ConversionRegistration(
                parent_target=cpp_target_ir,
                conversion_type=conv.typespec,
                include_location=cpp_target_header,
                namespace=cpp_target_ir.options.namespace,
            ),
            module.context,
        )


def _register_cpp_target_representations(cpp_target_ir: cpp_target.CppTarget, module: node.Module) -> None:
    """Register the representations in a CPP target."""
    # Start with the representations from the cpp_target statement
    for representation_ir in cpp_target_ir.representations:
        resolved_repr = representation_ir.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.typespec.instantiates is clkbuiltins.TACHYON:
            schema_arg = resolved_repr.typespec.arguments["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
            layout = tachyon_layout.layout_schema(module.context, schema_ir)
            tachyon_layout_reg.register_structured_type(module.context, schema_ir, layout)
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


def _register_cpp_target_representations_and_interfaces(
    cpp_target_ir: cpp_target.CppTarget, module: node.Module
) -> None:
    """Register the representations in a CPP target created from generate(cpp).

    Representations from generated CPP targets are paired with the interface because the order
    we render the interface has to match the order we registered the representation layout
    so that we don't render an interface before its dependencies have been rendered.

    The retry_errors flag is initially True so that we try to register any failed representations
    again after attempting to register the remaining unregistered representations. If we go through
    the loop and are unable to register any schemas we set retry_errors to False so that the user
    sees the error that is preventing registration to make progress.
    """
    registered_reprs_and_ifaces = []
    pending_reprs_and_ifaces = cpp_target_ir.representations_and_interfaces
    retry_errors = True
    while pending_reprs_and_ifaces:
        failed_reprs_and_ifaces = []
        for representation_ir, interface_ir in pending_reprs_and_ifaces:
            resolved_repr = representation_ir.get_resolved()
            assert resolved_repr.typespec.instantiates is clkbuiltins.TACHYON
            try:
                schema_arg = resolved_repr.typespec.arguments["schema"]
                assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
                schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
                if tachyon_reg.constraint_for_type(module.context, schema_ir) is None:
                    layout = tachyon_layout.layout_schema(module.context, schema_ir)
                    tachyon_layout_reg.register_structured_type(module.context, schema_ir, layout)
            except RuntimeError:
                if not retry_errors:
                    raise
                failed_reprs_and_ifaces.append((representation_ir, interface_ir))
                continue
            _register_representation(resolved_repr)
            registered_reprs_and_ifaces.append((representation_ir, interface_ir))
        if len(failed_reprs_and_ifaces) == len(pending_reprs_and_ifaces):
            retry_errors = False
        pending_reprs_and_ifaces = failed_reprs_and_ifaces
    cpp_target_ir.representations_and_interfaces = registered_reprs_and_ifaces


def _register_cpp_dial_schema_tags(
    dial: cpp_target.CppDial, module: node.Module, dial_header: context.Header, namespace: str
) -> None:
    """Register schema tags from a dial with the dial header.

    Similar to _register_cpp_target_schema_tags but for dial-specific schema tags.
    """
    registered_schema_tags = []
    pending_schema_tags = dial.schema_tags
    retry_errors = True
    while pending_schema_tags:
        failed_schema_tags = []
        for schema_tag in pending_schema_tags:
            assert isinstance(schema_tag.schema_ir, schema.Schema)
            generic_parameters = schema_tag.schema_ir.generic_parameters()
            if generic_parameters:
                try:
                    for param in generic_parameters:
                        if isinstance(param.default, typesys.TypeVal):
                            _ = cpp_typereg.get_cpp_type(module.context, param.default)
                except TypeError:
                    if not retry_errors:
                        raise
                    failed_schema_tags.append(schema_tag)
                    continue
                cpp_typereg.register_cpp_template(
                    module.context,
                    schema_tag.schema_ir,
                    types.CppTemplate(
                        includes=[dial_header],
                        cpp_namespace=namespace,
                        template_name=schema_tag.schema_ir.name,
                    ),
                )
            else:
                cpp_typereg.register_cpp_type(
                    module.context,
                    schema_tag.schema_ir,
                    types.CppType(
                        includes=[dial_header],
                        cpp_namespace=namespace,
                        type_name=schema_tag.schema_ir.name,
                    ),
                )
            registered_schema_tags.append(schema_tag)
        if len(failed_schema_tags) == len(pending_schema_tags):
            retry_errors = False
        pending_schema_tags = failed_schema_tags
    dial.schema_tags = registered_schema_tags


def _register_cpp_dial_representations(dial: cpp_target.CppDial, module: node.Module) -> None:
    """Register representations from a dial."""
    for representation_ir in dial.representations:
        resolved_repr = representation_ir.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.typespec.instantiates is clkbuiltins.TACHYON:
            schema_arg = resolved_repr.typespec.arguments["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
            if tachyon_reg.constraint_for_type(module.context, schema_ir) is None:
                layout = tachyon_layout.layout_schema(module.context, schema_ir)
                tachyon_layout_reg.register_structured_type(module.context, schema_ir, layout)


def _register_cpp_dial_interfaces(dial: cpp_target.CppDial, module: node.Module) -> None:
    """Register interfaces from a dial."""
    for interface_ir in dial.interfaces:
        schema_reg.register_interface(
            module.context,
            schema_reg.InterfaceInfo.make(interface_ir=interface_ir),
        )


def _register_cpp_target_dials(cpp_target_ir: cpp_target.CppTarget, module: node.Module) -> None:
    """Register all dial-specific entities (schema tags, representations, interfaces)."""
    for dial in cpp_target_ir.dials:
        if dial.cpp_cog.dial_header is None:
            msg = "Dial header was not set before registration"
            raise TypeError(msg)
        _register_cpp_dial_schema_tags(dial, module, dial.cpp_cog.dial_header, cpp_target_ir.options.namespace)
        _register_cpp_dial_representations(dial, module)
        _register_cpp_dial_interfaces(dial, module)


def _register_py_target(py_target_ir: py_target.PyTarget, module: node.Module) -> None:
    """Register the entities within a PyTarget.

    This is not meant to be used on its own, but as a helper function for
    compile_source_text().
    """
    py_import_spec = py_target_ir.import_spec()

    for repr_instantiation_ir in py_target_ir.representations:
        _register_representation(repr_instantiation_ir.get_resolved())

    for interface_ir in py_target_ir.interfaces:
        if (
            not isinstance(interface_ir.typespec, typesys.Instantiation)
            or interface_ir.typespec.instantiates != clkbuiltins.TAP
        ):
            msg = interface_ir.append_error_line("Only Tap<> interfaces are supported by python bindings.")
            raise NotImplementedError(msg)

        if not interface_ir.representation or not (
            representation_info := schema_reg.lookup_representation(module.context, interface_ir.representation)
        ):
            msg = interface_ir.append_error_line(
                "Unable to locate representation for interface. Is the representation defined in the .clk file?"
            )
            raise ValueError(msg)

        representation_ir = representation_info.representation_ir
        if representation_ir.typespec.instantiates != clkbuiltins.TACHYON:
            msg = representation_ir.append_error_line(
                "Only Tachyon<> representations are supported by python bindings."
            )
            raise NotImplementedError(msg)

        schema_ir = representation_ir.schema_ir
        if schema_ir.schema.generic_parameters():
            if not interface_ir.name:
                msg = interface_ir.append_error_line("Python interfaces for generic schemas must have an alias.")
                raise ValueError(msg)
            schema_name = interface_ir.name
        else:
            schema_name = schema_ir.schema_name

        py_typereg.register_py_type(
            module.context,
            schema_ir.as_instantiation_or_resolved_schema(),
            py_typereg.PyType(
                repo=module.module_id.repo,
                import_spec=py_import_spec,
                class_name=schema_name,
            ),
        )


def _register_cog_report_group_tachyon_layouts(module: node.Module, cog_ir: cog.Cog) -> None:
    """Register tachyon layouts for cog metrics report group schemas.

    This ensures that schemas dynamically generated by cog metrics report groups
    have tachyon constraint/layout entries even when no ``generate(cpp)`` target
    is present in the module.  User-defined report group schemas are handled by
    the normal cpp_target / dial registration path.
    """
    for report_group in cog_ir.cog_metrics_report_groups.values():
        for repr_inst in report_group.generated_representations:
            resolved_repr = repr_inst.get_resolved()
            if resolved_repr.typespec.instantiates is clkbuiltins.TACHYON:
                schema_arg = resolved_repr.typespec.arguments["schema"]
                assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
                schema_ir = schema.InstantiatedSchema.from_typespec(schema_arg)
                if tachyon_reg.constraint_for_type(module.context, schema_ir) is None:
                    layout = tachyon_layout.layout_schema(module.context, schema_ir)
                    tachyon_layout_reg.register_structured_type(module.context, schema_ir, layout)


def _register_cog_private_signals(compiler_context: CompilerContext, cog_ir: cog.Cog) -> None:
    """Register cog-private signals (those defined within report groups) in the global signal registry."""
    for report_group in (*cog_ir.report_groups.values(), *cog_ir.cog_metrics_report_groups.values()):
        for entry in report_group.entries.values():
            if entry.cog_private and isinstance(entry.signal, signal.Signal):
                resolved_signal = entry.signal.get_resolved()
                signal_registry.register_signal(compiler_context, resolved_signal)


# We must disable C901 and PLR0912 here (function complexity, branches) because
# we inherently have many branches, one for each type of module-level entity.
# However, they're handled in a uniform way that isn't difficult to understand.
# We could in principle make a data-driven table of handlers instead of explicit
# branches, but it would be awkward and would not decouple the code in a
# meaningful way.
def _resolve_entities(  # noqa: C901, PLR0912, PLR0915 (see above)
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

    for extern_type_ir in entities.extern_types:
        extern_type_ir.resolve()

    for enum_ir in entities.enums:
        enum_ir.resolve()

    for channel_ir in entities.channels:
        channel_ir.resolve()

    for policy_def in entities.policy_defs:
        policy_def.resolve()

    for policy_instance in entities.policy_instances:
        policy_instance.resolve()
        policy.register_policy(module, policy_instance)

    for trait_def in entities.dfl_trait_defs:
        trait_def.resolve()
        dfl_types.register_trait(module, trait_def)

    for trait_impl in entities.dfl_trait_impls:
        trait_impl.resolve()
        dfl_types.register_trait_impl(module, trait_impl)

    for cog_ir in entities.cogs:
        _prepare_aligned_input_names(cog_ir)
        cog_ir.resolve()
        _register_cog_private_signals(module.context, cog_ir)
        _validate_cog_aligned_inputs(cog_ir)
        _register_cog_report_group_tachyon_layouts(module, cog_ir)

    for instantiation_ir in entities.instantiations:
        instantiation_ir.resolve()
        if isinstance(instantiation_ir.instantiated, cog.InstantiatedCog):
            entities.cog_instantiations.append(instantiation_ir)
            cog.register_instantiated_cog(module.context, instantiation_ir.instantiated)
        elif isinstance(instantiation_ir.instantiated, box.InstantiatedBox):
            entities.box_instantiations.append(instantiation_ir)
        else:
            entities.schema_instantiations.append(instantiation_ir)

    for ethernet_lan_ir in entities.ethernet_lans:
        ethernet_lan_ir.resolve()

    for pcie_link_ir in entities.pcie_links:
        pcie_link_ir.resolve()
        hardware.register_pcie_link(pcie_link_ir, module)

    for representation_ir in entities.representations:
        representation_ir.resolve()
        if not representation_ir.name:
            # This is a default representation
            schema_reg.register_default_representation_options(module.context, representation_ir)

    for signal_ir in entities.signals:
        resolved_signal = signal_ir.resolve()
        signal_registry.register_signal(module.context, resolved_signal)

    for udp_socket_ir in entities.udp_socket:
        udp_socket_ir.resolve()

    for audio_source_ir in entities.audio_source:
        audio_source_ir.resolve()

    for proto_target_ir in entities.proto_targets:
        proto_target_ir.resolve()
        _register_proto_target(proto_target_ir, module)

    for cpp_target_ir in entities.cpp_targets:
        cpp_target_ir.resolve()
        _register_cpp_target(cpp_target_ir, module)

    for cpp_exe_ir in entities.cpp_executables:
        cpp_exe_ir.resolve()

    for nanobind_target_ir in entities.nanobind_targets:
        nanobind_target_ir.resolve()

    for py_target_ir in entities.py_targets:
        py_target_ir.resolve()
        _register_py_target(py_target_ir, module)

    for system_target_ir in entities.system_targets:
        resolved_system_target = system_target_ir.resolve()
        uuid_reg.register_entity_with_stable_key(module.context, system_target_ir)
        assert resolved_system_target.box_instance.source is not None
        _register_box_instance_uuids(module.context, resolved_system_target.box_instance.source)

    _register_entity_uuids(module.context, entities)


def _prepare_aligned_input_names(cog_ir: cog.Cog) -> None:
    """Resolve aligned inputs early and populate extra input names for metrics and infra diagnostics.

    This runs in the compiler because cog.py cannot import aligner.py (circular dep).
    Must run before Cog.resolve() so the names are available when metrics schemas
    and infra diagnostic signals are generated.
    """
    for aligned_def in cog_ir.aligned_inputs.values():
        aligned_def.resolve()
        if not isinstance(aligned_def.aligned_type, aligner.Aligner):
            continue
        cog_ir.extra_metrics_input_names.append(aligned_def.name)
        cog_ir.extra_diagnostics_input_names.append(aligned_def.name)
        if isinstance(aligned_def.view_params.max_msgs, int):
            cog_ir.extra_metrics_input_view_sizes[aligned_def.name] = aligned_def.view_params.max_msgs
        for inp in aligned_def.aligned_type.inputs.values():
            cog_ir.extra_metrics_input_names.append(inp.name)
            cog_ir.extra_diagnostics_input_names.append(inp.name)
            if (view_size := _aligned_consumer_max_msgs(aligned_def, inp)) is not None:
                cog_ir.extra_metrics_input_view_sizes[inp.name] = view_size


def _aligned_consumer_max_msgs(
    aligned_def: cog_components.CogAlignedInputDef,
    aligner_input: aligner.AlignerInputDef,
) -> int | None:
    aligner_max = aligner_input.view_params.max_msgs
    if not isinstance(aligner_max, int):
        return None
    override = aligned_def.upstream_view_overrides.get(aligner_input.name)
    if override is not None and override.is_user_set("max_msgs"):
        return override.max_msgs if isinstance(override.max_msgs, int) else None
    return _auto_size_aligned_consumer_max_msgs(aligner_max)


def _auto_size_aligned_consumer_max_msgs(aligner_max: int) -> int:
    """Compute the default consumer view size for an upstream aligned input."""
    # Auto-size consumer ``max_msgs`` to give headroom over the aligner:
    # at least one more slot, plus a 20% margin on top of the aligner value.
    return max(aligner_max + 1, math.ceil(aligner_max * 1.2))


def _validate_cog_aligned_inputs(cog_ir: cog.Cog) -> None:
    """Validate that all aligned_inputs on a cog reference actual aligner types.

    This validation runs in the compiler because cog.py cannot import aligner.py
    (circular dep). The compiler already imports both.
    """
    for aligned_input in cog_ir.aligned_inputs.values():
        if not isinstance(aligned_input.aligned_type, aligner.Aligner):
            msg = node.append_error_line(
                aligned_input.cst_node,
                aligned_input.module,
                f"aligned_inputs type must be an aligner, got {type(aligned_input.aligned_type).__name__}",
            )
            raise TypeError(msg)


def _expand_aligned_input_class_defs(cog_ir: cog.Cog) -> None:
    """Create class-level InputDefs for each upstream input of each aligned input group."""
    for aligned_def in cog_ir.aligned_inputs.values():
        aligner_type = aligned_def.aligned_type
        assert isinstance(aligner_type, aligner.Aligner), f"Expected Aligner, got {type(aligner_type).__name__}"

        assert aligner_type.resolved is not None, (
            f"Aligner '{aligner_type.name}' referenced by cog '{cog_ir.name}' is not resolved"
        )
        aligner_input_names = {inp.name for inp in aligner_type.inputs.values()}
        for override_name, override_params in aligned_def.upstream_view_overrides.items():
            if override_name not in aligner_input_names:
                msg = (
                    f"Aligned input '{aligned_def.name}' on cog '{cog_ir.name}' has an "
                    f"override for unknown upstream '{override_name}'. Known upstreams: "
                    f"{sorted(aligner_input_names)}."
                )
                if aligned_def.module.terminals is not None and override_params.cst_node is not None:
                    msg += node.append_error_line(
                        override_params.cst_node,
                        aligned_def.module,
                        "",
                    )
                raise ValueError(msg)
        for aligner_input in aligner_type.inputs.values():
            input_name = f"{aligned_def.name}.{aligner_input.name}"
            consumer_params = _synthesize_consumer_view_params(
                cog_ir=cog_ir,
                aligned_def=aligned_def,
                aligner_input=aligner_input,
            )
            # fmt: off
            input_def = cog.InputDef(
                # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                name=input_name,
                # pyrefly: ignore[unexpected-keyword] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                scope=cog_ir.inner_scope,
                type_info=clkbuiltins.COG_INPUT_TYPE,
                doc=aligner_input.doc,
                module=cog_ir.module,
                cst_node=None,
                message_type=aligner_input.get_interface_info(),
                view_params=consumer_params,
                is_generic=False,
                elements=None,
            )
            # fmt: on
            cog_ir.expanded_aligned_input_defs[input_name] = input_def


def _synthesize_consumer_view_params(  # noqa: C901  # Inherent complexity: one branch per overrideable view param.
    *,
    cog_ir: cog.Cog,
    aligned_def: cog_components.CogAlignedInputDef,
    aligner_input: aligner.AlignerInputDef,
) -> cog_components.ViewParams:
    """Compute the consumer's per-upstream view by overlaying the override on the aligner's view.

    Start from a copy of the aligner-side view so the consumer inherits
    ``safety_margin``, ``manual_cursor``, etc. Then overlay any user-set
    fields from the corresponding ``upstream_view_overrides`` entry. If
    ``max_msgs`` was not overridden, auto-size it from the aligner's
    ``max_msgs`` so the consumer has headroom over the aligner.

    Also validates that the final ``max_msgs`` is at least the aligner's.
    """
    aligner_params = aligner_input.view_params
    # Defensive: aligners require ``max_msgs`` to be explicit, so by the time we
    # reach this expansion the aligner-side ``max_msgs`` must already be resolved
    # to an int. Consumer views of aligner inputs are never ``manual_cursor`` and
    # never expose seqno.
    assert isinstance(aligner_params.max_msgs, int)
    consumer = cog_components.ViewParams(
        module=aligned_def.module,
        cst_node=None,
        max_msgs=aligner_params.max_msgs,
        manual_cursor=aligner_params.manual_cursor,
        no_dial=aligner_params.no_dial,
        skip_threshold=aligner_params.skip_threshold,
        safety_margin=aligner_params.safety_margin,
        copy_inputs=aligner_params.copy_inputs,
        is_optional=aligner_params.is_optional,
        expose_seqno=aligner_params.expose_seqno,
        use_device_ptr=aligner_params.use_device_ptr,
    )
    override = aligned_def.upstream_view_overrides.get(aligner_input.name)
    if override is not None:
        if override.is_user_set("max_msgs"):
            assert isinstance(override.max_msgs, int)
            consumer.max_msgs = override.max_msgs
        if override.is_user_set("manual_cursor"):
            consumer.manual_cursor = override.manual_cursor
        if override.is_user_set("no_dial"):
            consumer.no_dial = override.no_dial
        if override.is_user_set("skip_threshold"):
            consumer.skip_threshold = override.skip_threshold
        if override.is_user_set("safety_margin"):
            consumer.safety_margin = override.safety_margin
        if override.is_user_set("copy_inputs"):
            consumer.copy_inputs = override.copy_inputs
        if override.is_user_set("connect_optional"):
            consumer.is_optional = override.is_optional
    if override is None or not override.is_user_set("max_msgs"):
        aligner_max = aligner_params.max_msgs
        consumer.max_msgs = _auto_size_aligned_consumer_max_msgs(aligner_max)
    assert isinstance(consumer.max_msgs, int)
    if consumer.max_msgs < aligner_params.max_msgs:
        msg = (
            f"Aligned input '{aligned_def.name}.{aligner_input.name}' on cog "
            f"'{cog_ir.name}' has consumer max_msgs={consumer.max_msgs} which is "
            f"less than the aligner's max_msgs={aligner_params.max_msgs}. The "
            "consumer view is auto-sized by default; only set it explicitly when "
            "you need unusual headroom."
        )
        raise ValueError(msg)
    return consumer


def _generate_cpp_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    cpp_target_entities = cpp_target.CppGeneratedEntities(
        cogs=entities.cogs,
        cog_instantiations=entities.cog_instantiations,
        schemas=entities.schemas,
        enums=entities.enums,
        constants=[constant for constant in entities.constants if primitive.is_renderable_value(constant.value)],
        schema_instantiations=entities.schema_instantiations,
        tags=entities.tags,
        extern_types=entities.extern_types,
        strong_types=entities.strong_types,
        udp_sockets=entities.udp_socket,
        audio_sources=entities.audio_source,
    )
    cpp_target_ir = cpp_target.CppTarget.from_generate_cpp(module, cpp_target_entities)
    _register_cpp_target(cpp_target_ir, module)
    module.inner_scope.define(cpp_target_ir.name, cpp_target_ir, terminals)
    entities.cpp_targets.append(cpp_target_ir)


def _generate_py_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    py_target_entities = py_target.PyGeneratedEntities(
        schemas=entities.schemas,
        enums=entities.enums,
        constants=[constant for constant in entities.constants if primitive.is_renderable_value(constant.value)],
        schema_instantiations=entities.schema_instantiations,
    )
    py_target_ir = py_target.PyTarget.from_generate_py(module, py_target_entities)
    _register_py_target(py_target_ir, module)
    module.inner_scope.define(py_target_ir.name, py_target_ir, terminals)
    entities.py_targets.append(py_target_ir)


def _generate_py_cog_dial_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    py_target_ir = py_target.PyTarget.from_generate_py_cog(module, entities.cogs)
    _register_py_target(py_target_ir, module)
    module.inner_scope.define(py_target_ir.name, py_target_ir, terminals)
    entities.py_targets.append(py_target_ir)


def _generate_nanobind_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    nanobind_target_entities = nanobind_target.NanobindGeneratedEntities(
        schemas=entities.schemas,
        enums=entities.enums,
        constants=[constant for constant in entities.constants if primitive.is_renderable_value(constant.value)],
        schema_instantiations=entities.schema_instantiations,
    )
    nanobind_target_ir = nanobind_target.NanobindTarget.from_generate_nanobind(module, nanobind_target_entities)
    module.inner_scope.define(nanobind_target_ir.name, nanobind_target_ir, terminals)
    entities.nanobind_targets.append(nanobind_target_ir)


def _generate_proto_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    proto_target_entities = proto_target.ProtoGeneratedEntities(
        schemas=entities.schemas,
        enums=entities.enums,
        schema_instantiations=entities.schema_instantiations,
    )
    proto_target_ir = proto_target.ProtoTarget.from_generate_proto(module, proto_target_entities)
    _register_proto_target(proto_target_ir, module)
    module.inner_scope.define(proto_target_ir.name, proto_target_ir, terminals)
    entities.proto_targets.append(proto_target_ir)


def _generate_proto_conv_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    proto_conv_target_entities = cpp_target.ProtoConvGeneratedEntities(
        schemas=entities.schemas,
        schema_instantiations=entities.schema_instantiations,
    )
    proto_conv_target_ir = cpp_target.CppTarget.from_generate_proto_conv(module, proto_conv_target_entities)
    _register_cpp_target(proto_conv_target_ir, module)
    module.inner_scope.define(proto_conv_target_ir.name, proto_conv_target_ir, terminals)
    entities.cpp_targets.append(proto_conv_target_ir)


def _generate_cpp_exe_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    boxes: list[cpp_executable.CasingEntitySource] = []
    for bx in entities.boxes:
        if not bx.is_generic():
            assert isinstance(bx, cpp_executable.CasingEntitySource)
            boxes.append(bx)
    for inst in entities.box_instantiations:
        assert isinstance(inst.instantiated, cpp_executable.CasingEntitySource)
        boxes.append(inst.instantiated)
    cpp_exe_target_entities = cpp_executable.CppExecutableCasingEntities(
        boxes=boxes,
    )
    cpp_exe_target_ir = cpp_executable.CppExecutable.from_generate_cpp_exe(module, cpp_exe_target_entities)
    module.inner_scope.define(cpp_exe_target_ir.name, cpp_exe_target_ir, terminals)
    entities.cpp_executables.append(cpp_exe_target_ir)
    cpp_exe_target_ir.resolve()


def _generate_py_exe_target(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    """Generate a CPP exectable to run under a cc_binary_with_embedded_py bazel rule."""
    boxes: list[cpp_executable.CasingEntitySource] = []
    for bx in entities.boxes:
        if not bx.is_generic():
            assert isinstance(bx, cpp_executable.CasingEntitySource)
            boxes.append(bx)
    for inst in entities.box_instantiations:
        assert isinstance(inst.instantiated, cpp_executable.CasingEntitySource)
        boxes.append(inst.instantiated)
    py_exe_target_entities = cpp_executable.CppExecutableCasingEntities(
        boxes=boxes,
    )
    py_exe_target_ir = cpp_executable.CppExecutable.from_generate_py_exe(module, py_exe_target_entities)
    module.inner_scope.define(py_exe_target_ir.name, py_exe_target_ir, terminals)
    entities.cpp_executables.append(py_exe_target_ir)
    py_exe_target_ir.resolve()


@dataclass
class _AlignerInjectionContext:
    """Bundle of state shared across aligner-injection helpers."""

    main_target: cpp_target.CppTarget
    main_target_header: context.Header
    main_target_dial_header: context.Header
    generate_test_cogs: bool
    generate_combo: bool


def _resolve_aligner_main_target(module: node.Module, entities: ExtractedEntities) -> _AlignerInjectionContext:
    """Validate and locate the main ``_cc`` ``CppTarget`` for aligner injection."""
    assert module.generates is not None
    if node.GenerateTarget.cpp not in module.generates:
        msg = node.append_error_line(
            module.cst_node,
            module,
            "Modules with #![generate(cpp_aligner)] must also #![generate(cpp)] "
            + "so the alignment output schema can be placed in the _cc target.",
        )
        raise ValueError(msg)

    main_target_name = f"{module.module_id.name.split('::')[-1]}_clk_cc"
    main_target = next(
        (t for t in entities.cpp_targets if t.name == main_target_name),
        None,
    )
    assert main_target is not None, (
        f"Expected main CppTarget '{main_target_name}' in entities.cpp_targets "
        f"(found: {[t.name for t in entities.cpp_targets]})"
    )
    assert main_target.options is not None

    main_target_header = context.Header(
        module.module_id.repo, (module.module_id.get_base_path().parent / main_target.name).with_suffix(".hh")
    )
    main_target_dial_header = context.Header(
        main_target_header.repo, main_target_header.path.parent / (main_target_header.path.stem + "_dial.hh")
    )
    return _AlignerInjectionContext(
        main_target=main_target,
        main_target_header=main_target_header,
        main_target_dial_header=main_target_dial_header,
        generate_test_cogs=node.GenerateTarget.cpp_test_cog in module.generates,
        generate_combo=node.GenerateTarget.cpp_combo_test in module.generates,
    )


def _inject_aligner_cog_into_target(
    aligner_ir: aligner.Aligner,
    module: node.Module,
    ctx: _AlignerInjectionContext,
) -> None:
    """Create + register the synthetic cog for an aligner and attach it to the main target.

    Mirrors what ``_register_cpp_target`` /
    ``_handle_generated_cog_metrics`` /
    ``_register_report_group_outputs_on_dial`` do for cogs that were present
    during initial target registration, but only for this single
    aligner-derived synthetic cog.
    """
    assert aligner_ir.resolved is not None
    main_target = ctx.main_target
    assert main_target.options is not None
    synthetic_cog = make_aligner_cog(aligner_ir.resolved, module)
    uuid_reg.register_entity(module.context, synthetic_cog, synthetic_cog.fqn)
    for entity in chain(
        synthetic_cog.states.values(),
        synthetic_cog.inputs.values(),
        synthetic_cog.outputs.values(),
        synthetic_cog.conditions.values(),
        synthetic_cog.cog_metrics_report_groups.values(),
    ):
        uuid_reg.register_entity_with_stable_key(module.context, entity)
    aligner_ir.synthetic_cog = synthetic_cog

    cpp_typereg.register_cpp_type(
        module.context,
        synthetic_cog,
        types.CppType(
            includes=[ctx.main_target_header],
            cpp_namespace=main_target.options.namespace,
            type_name=synthetic_cog.name + "Factory",
        ),
    )

    cpp_cog = cpp_executable.CppCog(
        cog_ir=synthetic_cog,
        dial_header=ctx.main_target_dial_header,
        cog_header=ctx.main_target_header,
    )
    main_target.cogs.append(cpp_cog)
    new_dial = cpp_target.CppDial(cpp_cog=cpp_cog)
    main_target.dials.append(new_dial)
    main_target.aligner_cogs.append(synthetic_cog)
    main_target.aligner_sources.append(aligner_ir)

    main_target.representations.extend(synthetic_cog.generated_repr())
    main_target.interfaces.extend(synthetic_cog.generated_interfaces())
    main_target.schema_tags.extend(cpp_target.SchemaTag(schema_ir=s) for s in synthetic_cog.generated_schemas())
    main_target.enums.extend(cpp_target.EnumTarget(enum_ir=e) for e in synthetic_cog.generated_enums())
    new_dial.representations.extend(synthetic_cog.generated_report_group_repr())
    new_dial.interfaces.extend(synthetic_cog.generated_report_group_interfaces())
    new_dial.schema_tags.extend(
        cpp_target.SchemaTag(schema_ir=s) for s in synthetic_cog.generated_report_group_schemas()
    )

    _register_cpp_dial_schema_tags(new_dial, module, ctx.main_target_dial_header, main_target.options.namespace)
    _register_cpp_dial_representations(new_dial, module)
    _register_cpp_dial_interfaces(new_dial, module)

    if ctx.generate_test_cogs:
        main_target.cpp_test_cogs.append(cpp_target.CppTestCog(cpp_cog=cpp_cog, generate_combo_test=ctx.generate_combo))


def _inject_aligner_alignment_artifacts(
    aligner_ir: aligner.Aligner,
    module: node.Module,
    ctx: _AlignerInjectionContext,
    terminals: terminalsrc.TerminalSource,
) -> None:
    """Inject one aligner's alignment schema/repr/interface into the main ``_cc`` target.

    The C++ types for each injected artifact are registered immediately,
    since ``_register_cpp_target()`` already ran for ``main_target``.
    """
    main_target = ctx.main_target
    assert main_target.options is not None
    if aligner_ir.alignment_schema is not None:
        schema_source = aligner_ir.alignment_schema.schema.source
        assert schema_source is not None
        main_target.schema_tags.append(cpp_target.SchemaTag(schema_ir=schema_source))
        module.inner_scope.define(schema_source.name, schema_source, terminals)
        # Auto-generated <AlignerName>AlignmentMsg lives physically in
        # _cc_types.hh, but its public-facing include is the umbrella
        # _cc.hh (re-exported via IWYU pragma).  Same-module sub-files
        # that reference the schema have the umbrella stripped and
        # _cc_types.hh substituted in by ``_strip_umbrella_self_include``,
        # so registering against the umbrella is the right canonical
        # choice for both internal and external consumers.
        cpp_typereg.register_cpp_type(
            module.context,
            schema_source,
            types.CppType(
                includes=[ctx.main_target_header],
                cpp_namespace=main_target.options.namespace,
                type_name=schema_source.name,
            ),
        )
    if aligner_ir.alignment_repr is not None:
        main_target.representations.append(aligner_ir.alignment_repr)
        resolved_repr = aligner_ir.alignment_repr.get_resolved()
        _register_representation(resolved_repr)
        if resolved_repr.typespec.instantiates is clkbuiltins.TACHYON:
            schema_arg = resolved_repr.typespec.arguments["schema"]
            assert isinstance(schema_arg, schema.Schema | typesys.Instantiation)
            schema_ir_inst = schema.InstantiatedSchema.from_typespec(schema_arg)
            layout = tachyon_layout.layout_schema(module.context, schema_ir_inst)
            tachyon_layout_reg.register_structured_type(module.context, schema_ir_inst, layout)
    if aligner_ir.alignment_iface is not None:
        main_target.interfaces.append(aligner_ir.alignment_iface)
        schema_reg.register_interface(
            main_target.module.context,
            schema_reg.InterfaceInfo.make(interface_ir=aligner_ir.alignment_iface),
        )


def _generate_aligner_targets(
    module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource
) -> None:
    """Fold aligner-derived cogs into the main ``_cc`` ``CppTarget``.

    Each aligner produces a synthetic ``cog.Cog`` IR node that is appended
    to the main target's ``cogs``/``dials`` (and tracked in
    ``aligner_cogs``/``aligner_sources`` for impl generation).  The
    auto-generated alignment schema/repr/interface for each aligner is
    likewise injected into the main target so that ``<AlignerName>AlignmentMsg``
    lands in the same ``_cc_types.hh`` as user-declared schemas, keeping
    the umbrella ``_cc.hh`` as the single public include for consumers.
    """
    ctx = _resolve_aligner_main_target(module, entities)

    for aligner_ir in entities.aligners:
        _inject_aligner_cog_into_target(aligner_ir, module, ctx)

    for aligner_ir in entities.aligners:
        _inject_aligner_alignment_artifacts(aligner_ir, module, ctx, terminals)


def _populate_manual_aligner_cogs(module: node.Module, entities: ExtractedEntities) -> None:
    """For manual ``cpp_target`` blocks with aligner refs, create synthetic cogs.

    After ``CppTarget.resolve()`` the ``CppAlignerRef`` objects have resolved
    their name expressions to ``aligner.Aligner`` IR nodes.  After
    ``Aligner.resolve()`` each aligner has a ``resolved`` attribute.  This
    function creates synthetic ``cog.Cog`` nodes and adds them to the owning
    ``CppTarget``'s ``cogs``, ``dials``, and ``aligner_cogs`` lists — the same
    as if the user had declared ``cog Foo;`` in the target, except the
    ``_impl`` is auto-generated.
    """
    for cpp_target_ir in entities.cpp_targets:
        if not cpp_target_ir.aligner_refs:
            continue

        cpp_target_header = context.Header(
            cpp_target_ir.module.module_id.repo,
            BazelPathResolver().to_buildtime_path(cpp_target_ir.module.module_id).parent / (cpp_target_ir.name + ".hh"),
        )

        for ref in cpp_target_ir.aligner_refs:
            assert isinstance(ref.aligner_ir, aligner.Aligner)
            assert ref.aligner_ir.resolved is not None
            synthetic_cog = make_aligner_cog(ref.aligner_ir.resolved, module)
            uuid_reg.register_entity(module.context, synthetic_cog, synthetic_cog.fqn)
            for entity in chain(
                synthetic_cog.states.values(),
                synthetic_cog.inputs.values(),
                synthetic_cog.outputs.values(),
                synthetic_cog.conditions.values(),
                synthetic_cog.cog_metrics_report_groups.values(),
            ):
                uuid_reg.register_entity_with_stable_key(module.context, entity)
            ref.aligner_ir.synthetic_cog = synthetic_cog

            cpp_cog = cpp_executable.CppCog(cog_ir=synthetic_cog, dial_header=None, cog_header=cpp_target_header)
            # Register the cog C++ type (same as _register_cpp_target does
            # for cogs that were present during initial target registration).
            cpp_typereg.register_cpp_type(
                module.context,
                synthetic_cog,
                types.CppType(
                    includes=[cpp_target_header],
                    cpp_namespace=cpp_target_ir.options.namespace,
                    type_name=synthetic_cog.name + "Factory",
                ),
            )
            cpp_cog.dial_header = context.Header(
                cpp_target_header.repo,
                cpp_target_header.path.parent / (cpp_target_header.path.stem + "_dial.hh"),
            )
            cpp_target_ir.cogs.append(cpp_cog)
            cpp_target_ir.dials.append(cpp_target.CppDial(cpp_cog=cpp_cog))
            cpp_target_ir.aligner_cogs.append(synthetic_cog)
            cpp_target_ir.aligner_sources.append(ref.aligner_ir)

            if ref.aligner_ir.alignment_schema is not None:
                schema_source = ref.aligner_ir.alignment_schema.schema.source
                assert schema_source is not None
                cpp_target_ir.schema_tags.append(cpp_target.SchemaTag(schema_ir=schema_source))
            if ref.aligner_ir.alignment_repr is not None:
                cpp_target_ir.representations.append(ref.aligner_ir.alignment_repr)
            if ref.aligner_ir.alignment_iface is not None:
                cpp_target_ir.interfaces.append(ref.aligner_ir.alignment_iface)


def _generate_targets(module: node.Module, entities: ExtractedEntities, terminals: terminalsrc.TerminalSource) -> None:  # noqa: C901, PLR0912 (One condition/branch per target generated)
    if module.generates is None:
        return

    instantiation_aliases = set()
    for instantiation in entities.schema_instantiations:
        assert isinstance(instantiation.typespec, typesys.Instantiation)
        assert isinstance(instantiation.typespec.instantiates, schema.Schema)
        instantiation_alias = instantiation.name or instantiation.typespec.instantiates.name
        if instantiation_alias in instantiation_aliases:
            msg = node.append_error_line(
                instantiation.cst_node, module, f"Duplicate instantiation alias '{instantiation_alias}'"
            )
            raise ValueError(msg)
        instantiation_aliases.add(instantiation_alias)
    for schema_ir in entities.schemas:
        if schema_ir.programmatically_generated or schema_ir.parameters:
            continue
        if schema_ir.name in instantiation_aliases:
            msg = node.append_error_line(
                schema_ir.cst_node, module, f"Schema name '{schema_ir.name}' duplicates instantiation alias"
            )
            raise ValueError(msg)
        instantiation_aliases.add(schema_ir.name)

    if entities.cogs and node.GenerateTarget.cpp not in module.generates:
        msg = node.append_error_line(module.cst_node, module, "Modules that define cogs must generate cpp")
        raise ValueError(msg)

    if node.GenerateTarget.cpp in module.generates:
        _generate_cpp_target(module, entities, terminals)

    if node.GenerateTarget.py in module.generates:
        _generate_py_target(module, entities, terminals)

    if node.GenerateTarget.nanobind in module.generates:
        _generate_nanobind_target(module, entities, terminals)

    if node.GenerateTarget.proto in module.generates:
        _generate_proto_target(module, entities, terminals)

    if node.GenerateTarget.proto_conv in module.generates:
        _generate_proto_conv_target(module, entities, terminals)

    if node.GenerateTarget.cpp_exe in module.generates:
        _generate_cpp_exe_target(module, entities, terminals)

    if node.GenerateTarget.py_cog in module.generates:
        _generate_py_cog_dial_target(module, entities, terminals)

    if node.GenerateTarget.py_exe in module.generates:
        _generate_py_exe_target(module, entities, terminals)


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
    cpp_type = cpp_typereg.get_cpp_type(strong_type.module.context, strong_type.typespec)
    if not isinstance(cpp_type, types.CppType):
        msg = f"Expected a CppType for underlying type. Received: {cpp_type}"
        raise TypeError(cpp_type)
    cpp_typereg.register_cpp_type(strong_type.module.context, strong_type, cpp_type)

    protobuf_type = proto_typereg.get_protobuf_type(strong_type.typespec, compiler_context)
    proto_typereg.register_protobuf_type(strong_type, protobuf_type, compiler_context)
