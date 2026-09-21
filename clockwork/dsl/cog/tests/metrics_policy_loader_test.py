# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for metrics_policy_loader."""

from __future__ import annotations

from pathlib import Path
from typing import cast

import pytest
from clockwork.dsl.cog import metrics_policy_loader
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import compiler, importer_registry, node
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


class _FakeScope:
    """Minimal scope stub for extraction error tests."""

    def __init__(self, lookup_result: object | None) -> None:
        self.lookup_result = lookup_result
        self.lookup_name: str | None = None

    def lookup(self, name: str) -> object | None:
        self.lookup_name = name
        return self.lookup_result


class _FakeModule:
    """Minimal module stub for extraction error tests."""

    def __init__(self, lookup_result: object | None) -> None:
        self.module_id = ModuleID(CLK_REPO, "fake_policy_module")
        self.inner_scope = _FakeScope(lookup_result)


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling test modules."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def _context_with_importer(importer: node.Importer | None) -> CompilerContext:
    context = CompilerContext("metrics_policy_loader_test")
    context[importer_registry.IMPORTER_REGISTRY_KEY].importer = importer
    return context


def _fake_module(lookup_result: object | None) -> node.Module:
    return cast("node.Module", _FakeModule(lookup_result))


def test_load_policy_module_uses_filesystem_importer() -> None:
    calls: list[tuple[ModuleID, node.Importer]] = []
    compiled_module = cast("node.Module", object())

    def compile_fn(module_id: ModuleID, importer: node.Importer) -> node.Module:
        calls.append((module_id, importer))
        return compiled_module

    importer = FilesystemImporter(compile_fn=compile_fn)
    loaded_module = metrics_policy_loader.load_policy_module(
        _context_with_importer(importer),
        Path("std/cog_metrics_policy.clk"),
    )

    assert loaded_module is compiled_module
    assert calls == [(ModuleID(CLK_REPO, "std::cog_metrics_policy"), importer)]


def test_load_policy_module_requires_registered_importer() -> None:
    with pytest.raises(RuntimeError, match="No importer registered in compiler context"):
        metrics_policy_loader.load_policy_module(CompilerContext("no_importer"), Path("std/cog_metrics_policy.clk"))


def test_load_policy_module_requires_filesystem_importer() -> None:
    context = _context_with_importer(cast("node.Importer", object()))

    with pytest.raises(TypeError, match="Expected FilesystemImporter, got object"):
        metrics_policy_loader.load_policy_module(context, Path("std/cog_metrics_policy.clk"))


def test_extract_policy_class_returns_resolved_policy(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_text(
        """
// Test policy config.
schema TestPolicyConfig
{
    fields
    {
        // Whether the policy is enabled.
        #0 enabled: Bool = true;
    }
}

// Test policy.
def policy TestPolicy
{
    binds_to: Type;
    schema: TestPolicyConfig;
}
""",
        ModuleID(CLK_REPO, "metrics_policy_loader_extract_test"),
        fs_importer,
    )

    policy_class = metrics_policy_loader.extract_policy_class(module, "TestPolicy")

    assert policy_class.name == "TestPolicy"


def test_extract_policy_class_rejects_missing_policy() -> None:
    module = _fake_module(None)

    with pytest.raises(RuntimeError, match="Policy 'MissingPolicy' not found"):
        metrics_policy_loader.extract_policy_class(module, "MissingPolicy")


def test_extract_policy_class_rejects_non_policy_def() -> None:
    module = _fake_module(object())

    with pytest.raises(RuntimeError, match="is not a PolicyDef"):
        metrics_policy_loader.extract_policy_class(module, "NotAPolicy")
