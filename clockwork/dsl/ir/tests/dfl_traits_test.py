# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for DFL traits loader.

Tests the dfl_traits module which provides convenient access to
trait definitions from std/traits.clk.
"""

from __future__ import annotations

import pytest
from clockwork.dsl.ir import compiler, dfl_traits
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture(scope="module")
def fs_importer() -> FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


class TestTraitsLoader:
    """Tests for the dfl_traits loader module."""

    def test_ensure_traits_loaded_returns_registry(self, fs_importer: FilesystemImporter) -> None:
        """Test that ensure_traits_loaded returns a TraitsRegistry."""
        source = """\
#![generate()]
// Dummy module to get a compiler context
schema Dummy {
    uuid: 11111111-1111-1111-1111-111111111111;
    fields {
        // Value field
        #0 value: Int64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_traits_loader"), fs_importer)

        registry = dfl_traits.ensure_traits_loaded(module.context)
        assert registry is not None
        assert isinstance(registry, dfl_traits.TraitsRegistry)

    def test_all_standard_traits_loaded(self, fs_importer: FilesystemImporter) -> None:
        """Test that all standard traits are loaded."""
        source = """\
#![generate()]
// Dummy module
schema Dummy2 {
    uuid: 22222222-2222-2222-2222-222222222222;
    fields {
        // Timestamp field
        #0 ts: SyncTime;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_all_traits"), fs_importer)

        registry = dfl_traits.ensure_traits_loaded(module.context)
        entities = registry.entities

        assert entities.add_trait.name == "Add"
        assert entities.sub_trait.name == "Sub"
        assert entities.mul_trait.name == "Mul"
        assert entities.div_trait.name == "Div"
        assert entities.rem_trait.name == "Rem"
        assert entities.neg_trait.name == "Neg"
        assert entities.pos_trait.name == "Pos"
        assert entities.abs_trait.name == "Abs"
        assert entities.ord_trait.name == "Ord"
        assert entities.eq_trait.name == "Eq"
        assert entities.and_trait.name == "And"
        assert entities.or_trait.name == "Or"
        assert entities.not_trait.name == "Not"

    def test_ensure_traits_loaded_is_idempotent(self, fs_importer: FilesystemImporter) -> None:
        """Test that calling ensure_traits_loaded multiple times returns same registry."""
        source = """\
#![generate()]
// Dummy module for idempotent test
schema Dummy3 {
    uuid: 33333333-3333-3333-3333-333333333333;
    fields {
        // Some field
        #0 flag: Bool;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_idempotent"), fs_importer)

        registry1 = dfl_traits.ensure_traits_loaded(module.context)
        registry2 = dfl_traits.ensure_traits_loaded(module.context)

        assert registry1 is registry2

    def test_get_entities_convenience(self, fs_importer: FilesystemImporter) -> None:
        """Test the get_entities convenience function."""
        source = """\
#![generate()]
// Dummy module for get_entities
schema Dummy4 {
    uuid: 44444444-4444-4444-4444-444444444444;
    fields {
        // Duration field
        #0 duration: Duration;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_get_entities"), fs_importer)

        entities = dfl_traits.get_entities(module.context)
        assert entities is not None
        assert entities.add_trait is not None
        assert entities.ord_trait is not None

    def test_traits_have_correct_type_params(self, fs_importer: FilesystemImporter) -> None:
        """Test that loaded traits have correct type parameters."""
        source = """\
#![generate()]
// Dummy module for type params test
schema Dummy5 {
    uuid: 55555555-5555-5555-5555-555555555555;
    fields {
        // Value
        #0 value: Float64;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_type_params"), fs_importer)

        entities = dfl_traits.get_entities(module.context)

        # Binary traits have Rhs parameter with Self default
        add_resolved = entities.add_trait.resolve()
        assert len(add_resolved.type_params) == 1
        assert add_resolved.type_params[0].name == "Rhs"
        assert add_resolved.type_params[0].default_is_self is True

        # Unary traits have no type params
        neg_resolved = entities.neg_trait.resolve()
        assert len(neg_resolved.type_params) == 0

    def test_traits_have_associated_types(self, fs_importer: FilesystemImporter) -> None:
        """Test that loaded traits have the Output associated type."""
        source = """\
#![generate()]
// Dummy module for associated types
schema Dummy6 {
    uuid: 66666666-6666-6666-6666-666666666666;
    fields {
        // Counter
        #0 count: UInt32;
    }
}
"""
        module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_assoc_types"), fs_importer)

        entities = dfl_traits.get_entities(module.context)

        # All traits should have Output associated type
        for trait in [
            entities.add_trait,
            entities.sub_trait,
            entities.mul_trait,
            entities.ord_trait,
            entities.neg_trait,
        ]:
            resolved = trait.resolve()
            assert "Output" in resolved.associated_types, f"{trait.name} missing Output"
