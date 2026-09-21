# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Resolve source-declared Clockwork boxes to runtime cog instance paths."""

from __future__ import annotations

import json
import os
import subprocess
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, Literal, TypeAlias, final

from clockwork.dsl.composition import journal_topology
from clockwork.dsl.composition import system as composition_system
from clockwork.dsl.ir import box as box_ir
from clockwork.dsl.ir import cog as cog_ir
from clockwork.dsl.ir import compiler, importer, node
from clockwork.dsl.ir import system_target as system_target_ir
from clockwork.dsl.ir.module_id import CLK_REPO, ROOT_REPO, ModuleID
from clockwork.dsl.ir.path_resolver import BazelPathResolver, PathResolver

if TYPE_CHECKING:
    from collections.abc import Sequence

    from clockwork.tools.journal_file_generator.cog_channel import CogChannelMap

DiscoveryFormat: TypeAlias = Literal["table", "json"]

_VALID_OUTPUT_FORMATS: Final = ("table", "json")
_CLOCKWORK_PATH_MARKERS: Final = ("clockwork", "jewels", "std")
_WORKSPACE_ENV_VARS: Final = ("BUILD_WORKSPACE_DIRECTORY", "BUILD_WORKING_DIRECTORY")


@final
class DiscoveryValidationError(ValueError):
    """Raised when raw discovery options do not describe one valid request."""


@final
class DiscoveryError(RuntimeError):
    """Raised when discovery cannot resolve the requested source declaration."""


@final
@dataclass(frozen=True, kw_only=True)
class InstanceDiscoveryRequest:
    """Validated request for runtime cog instance discovery."""

    system_clk_file: Path | None
    """CLK file containing exactly one system_target, when supplied directly."""

    system_target: str | None
    """Future source for an existing compiled system or topology target."""

    source_clk_file: Path
    """CLK file containing the source-declared box filter."""

    box_name: str
    """Declared box name in source_clk_file."""

    cog_name: str | None
    """Optional declared cog instance name inside box_name."""

    output_format: DiscoveryFormat
    """Output format for CLI display."""


@final
@dataclass(frozen=True, kw_only=True)
class CogInstanceCandidate:
    """One runtime cog instance candidate matching a source declaration."""

    box_instance_path: str
    """Runtime box instance path containing the cog."""

    cog_instance_path: str
    """Fully resolved runtime cog instance path."""

    declared_box_name: str
    """Source-declared box name used as the discovery filter."""

    declared_cog_name: str
    """Source-declared cog instance name."""


@final
@dataclass(frozen=True, kw_only=True)
class _ClkModuleRef:
    """Compiler module ID plus an optional filesystem prefix for local paths."""

    module_id: ModuleID
    prefix_path: Path | None


@final
@dataclass(frozen=True, kw_only=True)
class _SystemContext:
    """Compiled system data needed for source-aware discovery."""

    top_boxes: tuple[box_ir.ResolvedBox, ...]
    runtime_cog_paths: frozenset[str]
    logical_system: composition_system.LogicalSystem


def create_instance_discovery_request(  # noqa: PLR0913 # Raw CLI values mirror the discovery request schema.
    *,
    system_clk_file: Path | None = None,
    system_target: str | None = None,
    source_clk_file: Path | None = None,
    box_name: str | None = None,
    cog_name: str | None = None,
    output_format: str = "table",
) -> InstanceDiscoveryRequest:
    """Validate raw CLI values and return an instance discovery request."""
    if (system_clk_file is None) == (system_target is None):
        msg = "Specify exactly one of --system-clk-file or --system-target."
        raise DiscoveryValidationError(msg)

    if system_target is not None:
        _validate_non_empty("--system-target", system_target)

    validated_source_clk_file = _validate_clk_file("--source-clk-file", source_clk_file)
    validated_box_name = _validate_required_option("--box-name", box_name)
    validated_cog_name = _validate_optional_non_empty("--cog-name", cog_name)
    validated_output_format = _validate_output_format(output_format)

    if system_clk_file is not None:
        system_clk_file = _validate_clk_file("--system-clk-file", system_clk_file)

    return InstanceDiscoveryRequest(
        system_clk_file=system_clk_file,
        system_target=system_target,
        source_clk_file=validated_source_clk_file,
        box_name=validated_box_name,
        cog_name=validated_cog_name,
        output_format=validated_output_format,
    )


def discover_cog_instances(request: InstanceDiscoveryRequest) -> list[CogInstanceCandidate]:
    """Discover runtime cog instance paths that match a source-declared box filter."""
    system_clk_file = _resolve_system_clk_file(request)

    source_ref = _module_ref_from_clk_path(request.source_clk_file)
    system_ref = _module_ref_from_clk_path(system_clk_file)
    path_resolver = _make_path_resolver((source_ref, system_ref))
    module_importer = _make_importer(path_resolver)

    try:
        source_module = _compile_module(source_ref, module_importer, path_resolver)
    except FileNotFoundError as exc:
        msg = f"Cannot load source CLK file: {request.source_clk_file}"
        raise DiscoveryError(msg) from exc

    selected_box = _find_declared_box(source_module, request.box_name, request.source_clk_file)

    try:
        system_module = _compile_module(system_ref, module_importer, path_resolver)
    except FileNotFoundError as exc:
        msg = f"Cannot load system CLK file: {system_clk_file}"
        raise DiscoveryError(msg) from exc

    system_context = _load_system_context(system_module, system_clk_file)
    matched_boxes = tuple(_iter_matching_box_instances(system_context.top_boxes, selected_box))
    if not matched_boxes:
        msg = (
            f"No runtime box instances in {system_clk_file} match declared box "
            f"'{request.box_name}' from {request.source_clk_file}."
        )
        raise DiscoveryError(msg)

    if request.cog_name is not None and not _has_direct_declared_cog(matched_boxes, request.cog_name):
        msg = f"Declared cog '{request.cog_name}' was not found inside box '{request.box_name}'."
        raise DiscoveryError(msg)

    candidates = _build_candidates(
        matched_boxes=matched_boxes,
        runtime_cog_paths=system_context.runtime_cog_paths,
        declared_box_name=request.box_name,
        declared_cog_name=request.cog_name,
    )
    if not candidates:
        msg = f"No runtime cog instance candidates matched declared box '{request.box_name}'."
        raise DiscoveryError(msg)
    return candidates


def discover_cog_channel_maps(
    *,
    system_clk_file: Path | None = None,
    system_target: str | None = None,
) -> dict[str, CogChannelMap]:
    """Discover input/output/snapshot member-to-channel maps from a compiled system."""
    if (system_clk_file is None) == (system_target is None):
        msg = "Specify exactly one of --system-clk-file or --system-target."
        raise DiscoveryValidationError(msg)
    if system_target is not None:
        _validate_non_empty("--system-target", system_target)
    if system_clk_file is not None:
        system_clk_file = _validate_clk_file("--system-clk-file", system_clk_file)

    system_context = _compile_system_context(_resolve_system_clk_file_options(system_clk_file, system_target))
    return journal_topology.make_cog_channel_maps(system_context.logical_system)


def expand_box_instance_paths(
    *,
    system_clk_file: Path | None = None,
    system_target: str | None = None,
    box_instance_paths: Sequence[str],
) -> tuple[str, ...]:
    """Expand resolved runtime box instance paths into contained cog instance paths."""
    if (system_clk_file is None) == (system_target is None):
        msg = "Specify exactly one of --system-clk-file or --system-target."
        raise DiscoveryValidationError(msg)
    if system_target is not None:
        _validate_non_empty("--system-target", system_target)
    if system_clk_file is not None:
        system_clk_file = _validate_clk_file("--system-clk-file", system_clk_file)

    requested_box_paths = tuple(sorted(set(box_instance_paths)))
    if not requested_box_paths:
        msg = "Specify --box-instance-path."
        raise DiscoveryValidationError(msg)
    for box_instance_path in requested_box_paths:
        _validate_non_empty("--box-instance-path", box_instance_path)

    system_context = _compile_system_context(_resolve_system_clk_file_options(system_clk_file, system_target))
    cog_instance_paths: list[str] = []
    for box_instance_path in requested_box_paths:
        resolved_box = _find_box_instance(system_context.top_boxes, box_instance_path)
        if resolved_box is None:
            msg = f"Box instance path '{box_instance_path}' was not found in the supplied system context."
            raise DiscoveryError(msg)
        box_cog_paths = [
            cog_instance.fqn
            for cog_instance in _collect_cogs_in_box(resolved_box)
            if cog_instance.fqn in system_context.runtime_cog_paths
        ]
        if not box_cog_paths:
            msg = f"Box instance path '{box_instance_path}' contains no runtime cog instances."
            raise DiscoveryError(msg)
        cog_instance_paths.extend(box_cog_paths)
    return tuple(sorted(set(cog_instance_paths)))


def format_discovery_candidates(candidates: list[CogInstanceCandidate], output_format: DiscoveryFormat) -> str:
    """Format discovery candidates as deterministic table or JSON text."""
    if output_format == "json":
        payload = [
            {
                "box_instance_path": candidate.box_instance_path,
                "cog_instance_path": candidate.cog_instance_path,
                "declared_box": candidate.declared_box_name,
                "declared_cog": candidate.declared_cog_name,
            }
            for candidate in candidates
        ]
        return json.dumps(payload, indent=2, sort_keys=True)

    headers = ("box_instance_path", "cog_instance_path", "declared_box", "declared_cog")
    rows = [
        (
            candidate.box_instance_path,
            candidate.cog_instance_path,
            candidate.declared_box_name,
            candidate.declared_cog_name,
        )
        for candidate in candidates
    ]
    widths = [max([len(header), *(len(row[index]) for row in rows)]) for index, header in enumerate(headers)]
    table_rows = [_format_table_row(headers, widths)]
    table_rows.extend(_format_table_row(row, widths) for row in rows)
    return "\n".join(table_rows)


def _validate_clk_file(option_name: str, value: Path | None) -> Path:
    if value is None:
        msg = f"Specify {option_name}."
        raise DiscoveryValidationError(msg)
    if value.suffix != ".clk":
        msg = f"{option_name} must refer to a .clk file."
        raise DiscoveryValidationError(msg)
    return value


def _validate_required_option(option_name: str, value: str | None) -> str:
    if value is None:
        msg = f"Specify {option_name}."
        raise DiscoveryValidationError(msg)
    _validate_non_empty(option_name, value)
    return value


def _validate_optional_non_empty(option_name: str, value: str | None) -> str | None:
    if value is None:
        return None
    _validate_non_empty(option_name, value)
    return value


def _validate_non_empty(option_name: str, value: str) -> None:
    if value == "":
        msg = f"{option_name} must not be empty."
        raise DiscoveryValidationError(msg)


def _validate_output_format(output_format: str) -> DiscoveryFormat:
    if output_format == "table" or output_format == "json":  # noqa: PLR1714 merging the comparisons weakens narrowing
        return output_format
    expected = ", ".join(_VALID_OUTPUT_FORMATS)
    msg = f"--format must be one of: {expected}."
    raise DiscoveryValidationError(msg)


def _resolve_system_clk_file(request: InstanceDiscoveryRequest) -> Path:
    return _resolve_system_clk_file_options(request.system_clk_file, request.system_target)


def _resolve_system_clk_file_options(system_clk_file: Path | None, system_target: str | None) -> Path:
    if system_clk_file is not None:
        return system_clk_file
    if system_target is None:
        msg = "Discovery requires --system-clk-file or --system-target."
        raise DiscoveryError(msg)
    return _resolve_system_target_to_clk_file(system_target)


def _resolve_system_target_to_clk_file(system_target: str) -> Path:
    if system_target.endswith(".clk"):
        _materialize_clk_file_label(system_target)
        return _clk_file_path_from_label(system_target)

    rule = _query_bazel_rule(system_target)
    rule_class = rule.attrib.get("class", "")
    if rule_class == "topology_summary":
        return _resolve_system_target_to_clk_file(_get_required_label_attr(rule, "system_target", system_target))
    if rule_class == "_clk":
        _materialize_bazel_target(system_target)
        return _clk_file_path_from_clk_rule(rule, system_target)

    msg = (
        f"--system-target must identify a Clockwork clk target or topology_summary target; "
        f"{system_target} has rule class '{rule_class}'."
    )
    raise DiscoveryError(msg)


def _materialize_clk_file_label(label: str) -> None:
    """Build the conventional clk target for a .clk source label, if present."""
    repo, package, target = _parse_bazel_label(label)
    clk_target_label = _format_bazel_label(repo, package, f"{Path(target).stem}_clk")
    try:
        rule = _query_bazel_rule(clk_target_label)
    except DiscoveryError:
        return
    if rule.attrib.get("class", "") == "_clk":
        _materialize_bazel_target(clk_target_label)


def _materialize_bazel_target(label: str) -> None:
    """Build a Bazel target so generated CLK dependencies are present in bazel-out."""
    process = subprocess.run(
        ["bazel", "build", label],
        cwd=_bazel_working_directory(),
        capture_output=True,
        text=True,
        check=False,
    )
    if process.returncode != 0:
        detail = process.stderr.strip() or process.stdout.strip() or f"bazel build failed with {process.returncode}"
        msg = f"Cannot build --system-target {label}: {detail}"
        raise DiscoveryError(msg)


def _query_bazel_rule(label: str) -> ET.Element:
    process = subprocess.run(
        ["bazel", "query", "--output=xml", label],
        cwd=_bazel_working_directory(),
        capture_output=True,
        text=True,
        check=False,
    )
    if process.returncode != 0:
        detail = process.stderr.strip() or process.stdout.strip() or f"bazel query failed with {process.returncode}"
        msg = f"Cannot resolve --system-target {label}: {detail}"
        raise DiscoveryError(msg)

    try:
        root = ET.fromstring(process.stdout)  # noqa: S314 Bazel query XML is generated by the local Bazel CLI.
    except ET.ParseError as exc:
        msg = f"Cannot parse bazel query output for --system-target {label}."
        raise DiscoveryError(msg) from exc

    rule = root.find("rule")
    if rule is None:
        msg = f"--system-target {label} did not resolve to a Bazel rule."
        raise DiscoveryError(msg)
    return rule


def _bazel_working_directory() -> str | None:
    for var_name in _WORKSPACE_ENV_VARS:
        value = os.getenv(var_name)
        if value:
            return value
    return None


def _get_required_label_attr(rule: ET.Element, attr_name: str, label: str) -> str:
    for child in rule:
        if child.tag == "label" and child.attrib.get("name") == attr_name:
            value = child.attrib.get("value")
            if value:
                return value
    msg = f"Target {label} does not have required label attribute '{attr_name}'."
    raise DiscoveryError(msg)


def _clk_file_path_from_clk_rule(rule: ET.Element, label: str) -> Path:
    src_labels = [
        label_child.attrib["value"]
        for list_child in rule
        if list_child.tag == "list" and list_child.attrib.get("name") == "srcs"
        for label_child in list_child
        if label_child.tag == "label" and "value" in label_child.attrib
    ]
    if len(src_labels) != 1:
        msg = f"Clockwork clk target {label} must have exactly one .clk source; found {len(src_labels)}."
        raise DiscoveryError(msg)
    return _clk_file_path_from_label(src_labels[0])


def _clk_file_path_from_label(label: str) -> Path:
    repo, package, target = _parse_bazel_label(label)
    if not target.endswith(".clk"):
        msg = f"Expected a .clk file label, got {label}."
        raise DiscoveryError(msg)
    path = Path(package) / target
    if repo in ("", ROOT_REPO, CLK_REPO):
        return path
    msg = f"Unsupported repository '{repo}' in CLK file label {label}."
    raise DiscoveryError(msg)


def _parse_bazel_label(label: str) -> tuple[str, str, str]:
    if "//" not in label:
        msg = f"Expected a Bazel label, got {label}."
        raise DiscoveryError(msg)

    repo = ""
    remaining_label = label
    if label.startswith("@"):
        repo, remaining_label = label[1:].split("//", maxsplit=1)
    else:
        remaining_label = label.split("//", maxsplit=1)[1]

    package, separator, target = remaining_label.partition(":")
    if not package:
        msg = f"Expected a Bazel package in label {label}."
        raise DiscoveryError(msg)
    if not separator:
        target = Path(package).name
    return repo, package, target


def _format_bazel_label(repo: str, package: str, target: str) -> str:
    repo_prefix = f"@{repo}" if repo else ""
    return f"{repo_prefix}//{package}:{target}"


def _module_ref_from_clk_path(clk_file: Path) -> _ClkModuleRef:
    parts = clk_file.parts
    if "platforms" in parts:
        for index in range(len(parts) - 1):
            if parts[index] == "platforms" and parts[index + 1] == "clockwork":
                module_path = Path(*parts[index + 2 :])
                prefix_path = Path(*parts[: index + 2])
                return _ClkModuleRef(module_id=ModuleID.from_path(CLK_REPO, module_path), prefix_path=prefix_path)

    for index, part in enumerate(parts):
        if part.endswith("clockwork+"):
            module_path = Path(*parts[index + 1 :])
            prefix_path = Path(*parts[: index + 1])
            return _ClkModuleRef(module_id=ModuleID.from_path(CLK_REPO, module_path), prefix_path=prefix_path)

    if not clk_file.is_absolute() and parts and parts[0] in _CLOCKWORK_PATH_MARKERS:
        return _ClkModuleRef(
            module_id=ModuleID.from_path(CLK_REPO, clk_file),
            prefix_path=Path("platforms/clockwork"),
        )

    if clk_file.is_absolute():
        workspace_path = _relative_to_workspace(clk_file)
        if workspace_path is None:
            msg = f"Cannot map absolute CLK path to a workspace-relative module path: {clk_file}"
            raise DiscoveryValidationError(msg)
        relative_path, prefix_path = workspace_path
        return _ClkModuleRef(module_id=ModuleID.from_path(ROOT_REPO, relative_path), prefix_path=prefix_path)

    return _ClkModuleRef(module_id=ModuleID.from_path(ROOT_REPO, clk_file), prefix_path=Path.cwd())


def _relative_to_workspace(clk_file: Path) -> tuple[Path, Path] | None:
    absolute_clk_file = clk_file.resolve()
    for workspace_root in _workspace_root_candidates(absolute_clk_file):
        try:
            relative_path = absolute_clk_file.relative_to(workspace_root.resolve())
        except ValueError:
            continue
        return relative_path, workspace_root
    return None


def _workspace_root_candidates(absolute_clk_file: Path) -> list[Path]:
    candidates = [Path(value) for var_name in _WORKSPACE_ENV_VARS if (value := os.getenv(var_name))]
    candidates.append(Path.cwd())
    candidates.extend(
        parent
        for parent in absolute_clk_file.parents
        if (parent / "MODULE.bazel").is_file() or (parent / "WORKSPACE").is_file()
    )
    return _unique_paths(candidates)


def _make_path_resolver(module_refs: tuple[_ClkModuleRef, ...]) -> BazelPathResolver:
    prefix_paths = _unique_paths(
        [
            *(module_ref.prefix_path for module_ref in module_refs if module_ref.prefix_path is not None),
            *_default_resolver_prefix_paths(),
        ]
    )
    return BazelPathResolver(prefix_paths=prefix_paths)


def _default_resolver_prefix_paths() -> list[Path]:
    anchors = [Path.cwd()]
    anchors.extend(Path(value) for var_name in _WORKSPACE_ENV_VARS if (value := os.getenv(var_name)))

    candidates: list[Path] = []
    for anchor in anchors:
        candidates.extend(_prefix_paths_from_anchor(anchor))
    return _unique_paths(candidates)


def _prefix_paths_from_anchor(anchor: Path) -> list[Path]:
    candidates = [anchor, anchor / "platforms/clockwork"]
    candidates.extend(_bazel_output_prefix_paths(anchor))
    candidates.extend(
        parent / "platforms/clockwork"
        for parent in (anchor, *anchor.parents)
        if (parent / "platforms/clockwork/clockwork").is_dir()
    )
    candidates.extend(
        parent
        for parent in (anchor, *anchor.parents)
        if (parent / "MODULE.bazel").is_file() and (parent / "clockwork").is_dir()
    )
    return candidates


def _bazel_output_prefix_paths(anchor: Path) -> list[Path]:
    candidates = [anchor / "bazel-bin"]
    candidates.extend(sorted((anchor / "bazel-out").glob("*/bin")))
    return [path for path in candidates if path.exists()]


def _unique_paths(paths: list[Path]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()
    for path in paths:
        normalized_path = path.resolve() if path.exists() else path
        if normalized_path in seen:
            continue
        seen.add(normalized_path)
        result.append(path)
    return result


def _make_importer(path_resolver: PathResolver) -> importer.FilesystemImporter:
    def compile_with_resolver(module_id: ModuleID, module_importer: node.Importer) -> node.Module:
        return compiler.compile_source_file(module_id, importer=module_importer, path_resolver=path_resolver)

    return importer.FilesystemImporter(compile_fn=compile_with_resolver, path_resolver=path_resolver)


def _compile_module(
    module_ref: _ClkModuleRef,
    module_importer: node.Importer,
    path_resolver: PathResolver,
) -> node.Module:
    return compiler.compile_source_file(module_ref.module_id, importer=module_importer, path_resolver=path_resolver)


def _compile_system_context(system_clk_file: Path) -> _SystemContext:
    system_ref = _module_ref_from_clk_path(system_clk_file)
    path_resolver = _make_path_resolver((system_ref,))
    module_importer = _make_importer(path_resolver)
    try:
        system_module = _compile_module(system_ref, module_importer, path_resolver)
    except FileNotFoundError as exc:
        msg = f"Cannot load system CLK file: {system_clk_file}"
        raise DiscoveryError(msg) from exc
    return _load_system_context(system_module, system_clk_file)


def _find_declared_box(source_module: node.Module, box_name: str, source_clk_file: Path) -> box_ir.BoxTemplate:
    found = source_module.inner_scope.lookup(box_name, recursive=False)
    if isinstance(found, box_ir.BoxTemplate):
        return found
    msg = f"Declared box '{box_name}' was not found in source CLK file {source_clk_file}."
    raise DiscoveryError(msg)


def _load_system_context(system_module: node.Module, system_clk_file: Path) -> _SystemContext:
    unresolved_system_targets = [
        obj
        for obj in system_module.inner_scope.names.values()
        if isinstance(obj, system_target_ir.UnresolvedSystemTarget)
    ]
    if not unresolved_system_targets:
        msg = f"System CLK file {system_clk_file} did not declare a system_target."
        raise DiscoveryError(msg)
    if len(unresolved_system_targets) > 1:
        msg = f"System CLK file {system_clk_file} declares multiple system_target blocks; expected exactly one."
        raise DiscoveryError(msg)

    resolved_system_target = unresolved_system_targets[0].get_resolved()
    logical_system = composition_system.make_system(
        [resolved_system_target.box_instance],
        resolved_system_target.module,
        resolved_system_target.require_logging_policies,
        resolved_system_target.use_simplelaunch,
    )
    runtime_cog_paths = frozenset(cog_instance.fqn for cog_instance in logical_system.cogs.values())
    return _SystemContext(
        top_boxes=(resolved_system_target.box_instance,),
        runtime_cog_paths=runtime_cog_paths,
        logical_system=logical_system,
    )


def _iter_matching_box_instances(
    boxes: tuple[box_ir.ResolvedBox, ...],
    selected_box: box_ir.BoxTemplate,
) -> tuple[box_ir.ResolvedBox, ...]:
    result: list[box_ir.ResolvedBox] = []
    for resolved_box in boxes:
        result.extend(_iter_matching_box_instances_in_box(resolved_box, selected_box))
    return tuple(result)


def _iter_matching_box_instances_in_box(
    resolved_box: box_ir.ResolvedBox,
    selected_box: box_ir.BoxTemplate,
) -> tuple[box_ir.ResolvedBox, ...]:
    result: list[box_ir.ResolvedBox] = []
    if resolved_box.source is not None and resolved_box.source.template is selected_box:
        result.append(resolved_box)
    for instance in resolved_box.instances:
        if isinstance(instance, box_ir.Box):
            result.extend(_iter_matching_box_instances_in_box(instance.get_resolved(), selected_box))
    return tuple(result)


def _has_direct_declared_cog(matched_boxes: tuple[box_ir.ResolvedBox, ...], cog_name: str) -> bool:
    return any(
        isinstance(instance, cog_ir.CogInstance) and instance.name == cog_name
        for matched_box in matched_boxes
        for instance in matched_box.instances
    )


def _build_candidates(
    *,
    matched_boxes: tuple[box_ir.ResolvedBox, ...],
    runtime_cog_paths: frozenset[str],
    declared_box_name: str,
    declared_cog_name: str | None,
) -> list[CogInstanceCandidate]:
    candidates = [
        CogInstanceCandidate(
            box_instance_path=_box_instance_path(cog_instance.fqn),
            cog_instance_path=cog_instance.fqn,
            declared_box_name=declared_box_name,
            declared_cog_name=cog_instance.name,
        )
        for matched_box in matched_boxes
        for cog_instance in _candidate_cogs_for_box(matched_box, declared_cog_name)
        if cog_instance.fqn in runtime_cog_paths
    ]
    return sorted(candidates, key=lambda candidate: candidate.cog_instance_path)


def _candidate_cogs_for_box(
    resolved_box: box_ir.ResolvedBox,
    declared_cog_name: str | None,
) -> tuple[cog_ir.CogInstance, ...]:
    if declared_cog_name is not None:
        return tuple(
            instance
            for instance in resolved_box.instances
            if isinstance(instance, cog_ir.CogInstance) and instance.name == declared_cog_name
        )
    return _collect_cogs_in_box(resolved_box)


def _find_box_instance(
    boxes: tuple[box_ir.ResolvedBox, ...],
    box_instance_path: str,
) -> box_ir.ResolvedBox | None:
    for resolved_box in boxes:
        if resolved_box.fqn == box_instance_path:
            return resolved_box
        for instance in resolved_box.instances:
            if not isinstance(instance, box_ir.Box):
                continue
            found = _find_box_instance((instance.get_resolved(),), box_instance_path)
            if found is not None:
                return found
    return None


def _collect_cogs_in_box(resolved_box: box_ir.ResolvedBox) -> tuple[cog_ir.CogInstance, ...]:
    result: list[cog_ir.CogInstance] = []
    for instance in resolved_box.instances:
        if isinstance(instance, cog_ir.CogInstance):
            result.append(instance)
        elif isinstance(instance, box_ir.Box):
            result.extend(_collect_cogs_in_box(instance.get_resolved()))
    return tuple(result)


def _box_instance_path(cog_instance_path: str) -> str:
    return cog_instance_path.rsplit(".", maxsplit=1)[0]


def _format_table_row(row: tuple[str, ...], widths: list[int]) -> str:
    return "  ".join(value.ljust(width) for value, width in zip(row, widths, strict=True))
