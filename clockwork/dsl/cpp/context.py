# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for dealing with C++ code generation context."""

from __future__ import annotations

import itertools
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from functools import total_ordering
from pathlib import Path, PurePath
from typing import TYPE_CHECKING, Final

from clockwork.dsl.bazel.cc_targets import CcBinary, CcBinaryWithEmbeddedPy, CcLibrary
from clockwork.dsl.bazel.clk_targets import module_to_clk
from clockwork.dsl.bazel.targets import Label
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO, ModuleID
from typing_extensions import override

if TYPE_CHECKING:
    import typing
    from collections.abc import Iterable


def comment_doc_string(docs: str) -> list[str]:
    """Prepend all lines in the docs string with ///."""
    return [f"/// {line}" for line in docs.split("\n")]


@total_ordering
@dataclass(eq=True, frozen=True, slots=True)
class Include(ABC):
    """Represents an include in the form of a header or a forward declaration."""

    @abstractmethod
    def __lt__(self, other: Include) -> bool:
        """Comparison operator."""

    @abstractmethod
    def render(self) -> str:
        """Convert Include to a string."""


@total_ordering
@dataclass(eq=True, frozen=True, slots=True)
class Header(Include):
    """A C++ header file.

    Attributes:
        path: The path to the header file (typically relative to repo root)
    """

    repo: str | None
    path: PurePath
    iwyu_pragma: str | None

    # pyrefly: ignore[missing-super-call] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    def __init__(self, repo: str | None, path: PurePath | str, iwyu_pragma: str | None = None) -> None:
        """Create a new Header.

        Args:
            repo: The repo where this header exists.
            path: The path to the header
            iwyu_pragma: Optional string for an IWYU pragma.
        """
        object.__setattr__(self, "repo", repo)
        object.__setattr__(self, "path", path if isinstance(path, PurePath) else PurePath(path))
        object.__setattr__(self, "iwyu_pragma", iwyu_pragma)

    @property
    def is_system(self) -> bool:
        """True if this is a system header (so that <> should be used instead of "")."""
        return False

    @property
    def quoted_path(self) -> str:
        """Render this header path as a string, including delimiters."""
        return f"<{self.path}>" if self.is_system else f'"{self.path}"'

    @override
    def render(self) -> str:
        """Render this header path as a string, including delimiters."""
        base_include = f"#include {self.quoted_path}"
        if self.iwyu_pragma:
            return f"{base_include} // {self.iwyu_pragma}"
        return base_include

    @override
    def __lt__(self, other: Include) -> bool:
        """Implement ordering with system headers first, then lexicographic."""
        if not isinstance(other, Header):
            return True
        if not self.is_system and other.is_system:
            return True
        if self.is_system and not other.is_system:
            return False
        # Ordering is independent of repo as C++ include statements are path only.
        return self.path < other.path

    def target_label(self, current_repo: str) -> Label | None:
        """Get the target label for this header."""
        if self.path == PurePath("wise_enum.h"):
            return Label("@wise_enum")
        if self.is_system:
            return None
        package = self.path.parent if self.path.parent != Path() else ""
        # fmt: off
        name = (
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            str(self.path.name).replace(".pb.h", "_cc_library") if str(self.path).endswith(".pb.h") else self.path.stem
        )
        # fmt: on
        if self.repo != current_repo:
            return Label(f"@{self.repo}//{package}:{name}")
        return Label(f"//{package}:{name}")


class MaybeHeader(Header):
    """A C++ header file that is included if it exists."""

    @override
    def render(self) -> str:
        """Render this header path as a string, including delimiters."""
        return "\n".join((f"#if __has_include({self.quoted_path})", Header.render(self), "#endif"))


class SystemHeader(Header):
    """A C++ system header file.

    The only difference between this and Header is that system headers are
    included using angle brackets <> rather than double-quotes.
    """

    def __init__(self, path: PurePath | str, iwyu_pragma: str | None = None) -> None:
        """Construct a system header."""
        super().__init__(repo=None, path=path, iwyu_pragma=iwyu_pragma)

    @property
    @override
    def is_system(self) -> bool:
        """True if this is a system header (so that <> should be used instead of "")."""
        return True


@total_ordering
@dataclass(eq=True, frozen=True, slots=True)
class FwdDecl(Include):
    """A forward declaration."""

    namespace: str
    decl: str

    @override
    def render(self) -> str:
        """Render declaration as a string."""
        return f"namespace {self.namespace} {{ {self.decl}; }} // IWYU pragma: keep"

    @override
    def __lt__(self, other: Include) -> bool:
        """Implement ordering with all headers first, then lexicographic."""
        if not isinstance(other, FwdDecl):
            return False
        return (self.namespace, self.decl) < (other.namespace, other.decl)


class CppContext:
    """Class representing the context needed for generating C++ code.

    Attributes:
        includes: The includes that need to be included within this context.
    """

    meta_includes: typing.ClassVar = {
        Header(CLK_REPO, "clockwork/cog/include_common.hh"): {
            Header(JEWELS_REPO, "jewels/container/compare.hh"),
            Header(JEWELS_REPO, "jewels/memory/pointers.hh"),
            Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
            Header(JEWELS_REPO, "jewels/std/expected.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_conditions.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_configs.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_diagnostics.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_inputs.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_memory_resources.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_publishers.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_states.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_statistics.hh"),
            Header(CLK_REPO, "clockwork/cog/cog_timers.hh"),
            Header(CLK_REPO, "clockwork/cog/input_condition.hh"),
            Header(CLK_REPO, "clockwork/cog/input_view.hh"),
            Header(CLK_REPO, "clockwork/cog/simple_cog.hh"),
            Header(CLK_REPO, "clockwork/common/abstract_cog.hh"),
            Header(CLK_REPO, "clockwork/common/abstract_cog_queue.hh"),
            Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh"),
            Header(CLK_REPO, "clockwork/repr_iface.hh"),
        },
        Header(CLK_REPO, "clockwork/dial/include_common.hh"): {
            Header(JEWELS_REPO, "jewels/callsig/outcome.hh"),
            Header(JEWELS_REPO, "jewels/callsig/outparam.hh"),
            Header(JEWELS_REPO, "jewels/container/tap/soa.hh"),
            Header(JEWELS_REPO, "jewels/memory/aligned_storage.hh"),
            Header(JEWELS_REPO, "jewels/memory/memory_resource.hh"),
            Header(JEWELS_REPO, "jewels/memory/pointers.hh"),
            Header(JEWELS_REPO, "jewels/meta/concepts.hh"),
            Header(JEWELS_REPO, "jewels/time/sync_time.hh"),
            Header(JEWELS_REPO, "jewels/uuid/uuid.hh"),
            Header(CLK_REPO, "clockwork/dial/cond_messages_present.hh"),
            Header(CLK_REPO, "clockwork/dial/cond_time_since_last_exec.hh"),
            Header(CLK_REPO, "clockwork/dial/msg_input.hh"),
            Header(CLK_REPO, "clockwork/pinion/abstract_channel.hh"),
            Header(CLK_REPO, "clockwork/dial/signal_aggregator.hh"),
            Header(CLK_REPO, "clockwork/dsl/cog/common_cog_event_metrics_clk_cc.hh"),
            Header(CLK_REPO, "clockwork/dsl/cog/common_cog_telemetry_metrics_clk_cc.hh"),
            Header(CLK_REPO, "clockwork/dsl/cog/ten_nanosecond_clk_cc.hh"),
            Header(CLK_REPO, "clockwork/pinion/publishable.hh"),
            Header(CLK_REPO, "clockwork/pinion/publisher_handle.hh"),
            Header(CLK_REPO, "clockwork/repr_iface.hh"),
            Header(CLK_REPO, "clockwork/tags.hh"),
        },
    }

    def __init__(self) -> None:
        """Construct a new context."""
        self.includes: set[Include] = set()

    def add_include(self, include: Include) -> None:
        """Adds a C++ include to the context.

        The include statement is added in the form of '<include_path>' if it's part of the
        standard library, otherwise it's added as '"include_path"'.

        Args:
            include: The include to include.
        """
        self.includes.add(include)

    def add_includes(self, includes: Iterable[Include]) -> None:
        """Adds multiple C++ include to the context.

        Args:
            includes: An iterable of includes to add.
        """
        for include in includes:
            self.add_include(include)

    def update(self, other: CppContext) -> None:
        """Incorporate another context into this one."""
        self.includes |= other.includes

    def get_includes(self) -> set[Include]:
        """Returns a minimized list of includes, eliminating known redundancies."""
        includes = set(self.includes)
        for meta_inc, contents in self.meta_includes.items():
            if meta_inc in includes:
                includes -= contents
        return includes

    def render_includes(self) -> Iterable[str]:
        """Renders the include statements, sorted alphabetically.

        Returns:
            An iterator over C++ include statements.
        """
        return (include.render() for include in sorted(self.get_includes()))

    def remove_include(self, include: Include) -> None:
        """Remove a include."""
        self.includes.discard(include)


SPACES_PER_INDENT: Final = 4
INDENT: Final = " " * SPACES_PER_INDENT


@dataclass(slots=True)
class CppChunk:
    """A chunk of C++ source code, with contextual requirements.

    Note:
        The produce flag here is used to track whether this chunk of code
        should ultimately be emitted by the compiler, based on whether it contains
        any meaningful code. Most code is meaningful; examples of meaningless code
        include an empty namespace block or "#pragma once".

    Attributes:
        context: The CppContext object representing contextual requirements for the chunk.
        lines: List of strings, each representing a line of C++ code in this chunk.
        produce: If True (default), this indicates the chunk contains meaningful code and should be produced (see note).
    """

    context: CppContext = field(default_factory=CppContext)
    lines: list[str] = field(default_factory=list)
    produce: bool = False

    def append(self, chunk: str | list[str] | CppChunk, indent: int = 0, causes_production: bool = True) -> None:
        """Add code to this chunk.

        Args:
            chunk: The code to add.
            indent: Number of levels of indentation for added code.
            causes_production: If True (default), this is meaningful code (see explanation in class).
        """
        self.produce = self.produce or causes_production
        if isinstance(chunk, str):
            lines = [chunk]
        elif isinstance(chunk, list):
            lines = chunk
        else:
            self.context.update(chunk.context)
            self.produce = self.produce or chunk.produce
            lines = chunk.lines
        indent_str = INDENT * indent
        self.lines.extend(indent_str + line for line in lines)

    def render_str(self, render_includes: bool = False, pragma_once: bool = False) -> str:
        """Render this chunk as a string."""
        preamble = ["#pragma once"] if pragma_once else []
        preamble += self.context.render_includes() if render_includes else []
        # TODO(OI-1756): Remove readability-identifier-naming nolint
        preamble += ["// NOLINTBEGIN(readability-magic-numbers,readability-identifier-naming)"] if pragma_once else []
        # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        postamble = ["// NOLINTEND(readability-magic-numbers,readability-identifier-naming)"] if pragma_once else []
        return "".join(line + "\n" for line in itertools.chain(preamble, self.lines, postamble))


@dataclass(slots=True)
class CppModuleChunks:
    """A set of related chunks of C++ code for a C++ module.

    In this context, a "module" is a combination of a header file, an inline
    file, and an implementation file.

    Attributes:
        header_chunk: The header file chunk.
        inline_chunk: The inline file chunk.
        implementation_chunk: The implementation file chunk.
    """

    header_chunk: CppChunk = field(default_factory=CppChunk)
    inline_chunk: CppChunk = field(default_factory=CppChunk)
    implementation_chunk: CppChunk = field(default_factory=CppChunk)

    def append(  # noqa: PLR0913
        self,
        chunk: str | list[str] | CppChunk | CppModuleChunks,
        *,
        indent: int = 0,
        header_indent: int | None = None,
        inline_indent: int | None = None,
        implementation_indent: int | None = None,
        causes_production: bool | None = None,
    ) -> None:
        """Append a chunk to this module.

        Depending on the type of the 'chunk' parameter, this method operates in different modes:
        - If it is str, list[str], or CppChunk: the provided code is added to all parts of the module.
        - If it is a CppModuleChunks: its constituent chunks are appended to corresponding parts of this module.

        For str or list[str] inputs, 'causes_production' indicates whether the appended code is meaningful and
        thus should result in output (default is True). For CppChunk or CppModuleChunks, existing 'produce' flags
        are respected, and 'causes_production' should not be provided.

        Args:
            chunk: The chunk or module to append.
            indent: Default indentation level for all module parts.
            header_indent: Specific indentation level for the header part (overrides 'indent').
            inline_indent: Specific indentation level for the inline part (overrides 'indent').
            implementation_indent: Specific indentation level for the implementation part (overrides 'indent').
            causes_production: Specifies if the operation marks the chunk as meaningful (defaults to True for str/list inputs).
        """
        if header_indent is None:
            header_indent = indent
        if inline_indent is None:
            inline_indent = indent
        if implementation_indent is None:
            implementation_indent = indent
        if isinstance(chunk, str):
            chunk = [chunk]
        if isinstance(chunk, list):
            causes_production = True if causes_production is None else causes_production
            chunk = CppChunk(context=CppContext(), lines=chunk, produce=causes_production)
            causes_production = None
        if isinstance(chunk, CppChunk):
            chunk = CppModuleChunks(header_chunk=chunk, inline_chunk=chunk, implementation_chunk=chunk)
        if causes_production is not None:
            msg = "Cannot specify causes_production in combination with CppChunk or CppModuleChunks."
            raise ValueError(msg)
        self.header_chunk.append(chunk.header_chunk, indent=header_indent, causes_production=False)
        self.inline_chunk.append(chunk.inline_chunk, indent=inline_indent, causes_production=False)
        self.implementation_chunk.append(
            chunk.implementation_chunk,
            indent=implementation_indent,
            causes_production=False,
        )


def as_cc_library(cpp_mod: CppModuleChunks, name: str, package: Path, module_id: ModuleID, testonly: bool) -> CcLibrary:
    """Generate a CcLibrary target."""
    suffixes = ("hh", "inl", "cc")
    # Paths are relative to the module's directory.
    header_file, inline_file, impl_file = (Path(f"{name}.{suffix}") for suffix in suffixes)

    hdrs = [header_file]
    srcs = [inline_file, impl_file]

    chunks = (cpp_mod.header_chunk, cpp_mod.inline_chunk, cpp_mod.implementation_chunk)
    deps = set()
    this_target_label = Label(f"//{package}:{name}")
    for chunk in chunks:
        for include in chunk.context.get_includes():
            if (
                isinstance(include, Header)
                and not isinstance(include, MaybeHeader)
                and (target_label := include.target_label(module_id.repo))
                and target_label != this_target_label
            ):
                deps.add(target_label)

    return CcLibrary(
        name=name,
        hdrs=hdrs,
        srcs=srcs,
        deps=sorted(deps),
        data=[
            module_to_clk(module_id.repo, module_id),
        ],
        testonly=testonly,
    )


def as_cc_binary(cpp_mod: CppModuleChunks, name: str, package: Path, module_id: ModuleID) -> CcBinary:
    """Generate a CcBinary target."""
    # A cc_binary is pretty much a cc_library without headers.
    # Construct a cc_library and shove the hdrs into srcs.
    cc_library = as_cc_library(cpp_mod, name, package, module_id, False)

    clk_target = module_to_clk(module_id.repo, module_id)
    data = [*cc_library.data]
    data.remove(clk_target)

    return CcBinary(
        name=cc_library.name,
        srcs=[*cc_library.hdrs, *cc_library.srcs],
        deps=cc_library.deps,
        # The library adds an extra dependency on the clockwork file target
        data=data,
    )


def as_cc_binary_with_embedded_py(
    cpp_mod: CppModuleChunks, name: str, package: Path, py_deps: list[Label], module_id: ModuleID
) -> CcBinaryWithEmbeddedPy:
    """Generate a CcBinary target."""
    # A cc_binary_with_embedded_py is pretty much  a cc_binary with py_deps.
    # Construct a cc_binary and add py_deps.
    cc_binary = as_cc_binary(cpp_mod, name, package, module_id)

    return CcBinaryWithEmbeddedPy(
        name=cc_binary.name,
        srcs=cc_binary.srcs,
        deps=cc_binary.deps,
        data=cc_binary.data,
        py_deps=py_deps,
    )


def write_to_file(  # noqa: PLR0913 # too many args mitigated by kwonly args
    rendered_cpp_mod: CppModuleChunks,
    *,
    write_dir: Path,
    include_dir: Path,
    stem: str,
    current_repo: str,
    iwyu_private_redirect: Header | None = None,
    iwyu_friend_pattern: str | None = None,
) -> None:
    """Write cpp module chunks to a file.

    Args:
        rendered_cpp_mod: The rendered c++ code to write out to a file.
        write_dir: The directory to write the files to.
        include_dir: The include path of the files relative to root_dir.
        stem: The stem for the filename of each output.
        current_repo: The repo for the current file being compiled.
        iwyu_private_redirect: When provided, mark the generated header as
            ``IWYU pragma: private`` and direct IWYU to suggest the given
            header to consumers instead.  Used for sub-headers of the
            split ``_cc`` family (``_cc_types.hh``, ``_cc_cog.hh``) so
            external consumers are routed to the umbrella ``_cc.hh``.
        iwyu_friend_pattern: Optional regex (ECMAScript flavor, as used
            by ``IWYU pragma: friend``) of paths whose include of this
            header should be permitted despite the private redirect.
            Required whenever sibling generated headers in the same
            module need to include this header directly to avoid an
            include cycle through the umbrella.
    """
    header_path = (write_dir / stem).with_suffix(".hh")
    inline_path = (write_dir / stem).with_suffix(".inl")
    implementation_path = (write_dir / stem).with_suffix(".cc")

    header_header = Header(repo=current_repo, path=(include_dir / stem).with_suffix(".hh"))
    inline_header = Header(repo=current_repo, path=(include_dir / stem).with_suffix(".inl"))

    cpp_mod = CppModuleChunks()
    cpp_mod.inline_chunk.append(f'// IWYU pragma: private, include "{header_header.path}"')
    cpp_mod.inline_chunk.context.add_include(header_header)
    cpp_mod.implementation_chunk.context.add_include(header_header)

    if iwyu_private_redirect is not None:
        cpp_mod.header_chunk.append(f'// IWYU pragma: private, include "{iwyu_private_redirect.path}"')
        if iwyu_friend_pattern is not None:
            cpp_mod.header_chunk.append(f'// IWYU pragma: friend "{iwyu_friend_pattern}"')

    # The actual code we want to write to the file.
    cpp_mod.append(rendered_cpp_mod)

    # Have to manually add this one at the end as all includes are
    # raised to the top otherwise and this one needs to be at the end.
    cpp_mod.header_chunk.append(f"{inline_header.render()} // IWYU pragma: keep")

    # Cannot include itself.
    cpp_mod.header_chunk.context.remove_include(header_header)

    with header_path.open("w") as f:
        f.write(cpp_mod.header_chunk.render_str(render_includes=True, pragma_once=True))

    with inline_path.open("w") as f:
        f.write(cpp_mod.inline_chunk.render_str(render_includes=True, pragma_once=True))

    with implementation_path.open("w") as f:
        f.write(cpp_mod.implementation_chunk.render_str(render_includes=True))
