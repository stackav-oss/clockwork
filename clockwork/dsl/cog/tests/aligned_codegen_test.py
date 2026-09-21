# Copyright 2026 Stack AV Co.
# pyright: reportPrivateUsage=false

"""Golden file tests for aligned input C++ codegen (cog policy + dial structs)."""

from __future__ import annotations

from pathlib import Path

import pytest
from clockwork.dsl.cog import cppcog, cppdial
from clockwork.dsl.cog.tests.support import test_helpers
from clockwork.dsl.cpp.context import Header
from clockwork.dsl.ir import cog, compiler, importer
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID

# Set to True and ``bazel run`` to update expectation files. Review the diff
# before committing!
UPDATE_EXPECTATIONS = False


@pytest.fixture(scope="module")
def fs_importer() -> importer.FilesystemImporter:
    """Create a filesystem importer for compiling .clk files."""
    return importer.FilesystemImporter(compile_fn=compiler.compile_source_file)


@pytest.fixture(scope="module")
def consumer_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.Cog:
    """Compile aligned_consumer_cog.clk and return the ConsumerCog IR."""
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/composition/tests/support/aligned_consumer_cog.clk")),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("ConsumerCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


@pytest.fixture(scope="module")
def multiple_aligned_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.Cog:
    """Compile a cog that consumes two independent alignment groups."""
    module = compiler.compile_source_file(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/dsl/composition/tests/support/multiple_aligned_consumer_cog.clk"),
        ),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("MultipleAlignedConsumerCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


@pytest.fixture(scope="module")
def combined_aligned_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.Cog:
    """Compile a cog that runs when either of two available alignment groups is new."""
    module = compiler.compile_source_file(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/dsl/composition/tests/support/combined_aligned_consumer_cog.clk"),
        ),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("CombinedAlignedConsumerCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


@pytest.fixture(scope="module")
def empty_aligned_cog_ir(fs_importer: importer.FilesystemImporter) -> cog.Cog:
    """Compile a cog whose aligned group is permitted to be empty."""
    module = compiler.compile_source_file(
        ModuleID.from_path(
            CLK_REPO,
            Path("clockwork/dsl/composition/tests/support/empty_aligned_consumer_cog.clk"),
        ),
        importer=fs_importer,
    )
    cog_ir = module.inner_scope.lookup("EmptyAlignedConsumerCog")
    assert isinstance(cog_ir, cog.Cog)
    return cog_ir


def test_aligned_cog_policy_header(consumer_cog_ir: cog.Cog) -> None:
    """Golden file test: cog policy header with mixed regular and aligned input policies."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/composition/tests/support/aligned_consumer_cog_dial.hh")
    cpp_cog = cppcog.Cog.make(
        cog_ir=consumer_cog_ir,
        class_name="ConsumerCog",
        dial_name=None,
        header_name="consumer_cog.hh",
        cpp_namespace="clockwork::testing::aligned_consumer",
        dial_header=dial_header,
    )
    cpp_mod = cpp_cog.render()
    actual = cpp_mod.header_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_aligned_cog_header", actual)
    assert actual == test_helpers.load_expected("expected_aligned_cog_header")


def test_aligned_cog_policy_source(consumer_cog_ir: cog.Cog) -> None:
    """Golden file test: cog policy source with make_dial for mixed regular and aligned inputs."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/composition/tests/support/aligned_consumer_cog_dial.hh")
    cpp_cog = cppcog.Cog.make(
        cog_ir=consumer_cog_ir,
        class_name="ConsumerCog",
        dial_name=None,
        header_name="consumer_cog.hh",
        cpp_namespace="clockwork::testing::aligned_consumer",
        dial_header=dial_header,
    )
    cpp_mod = cpp_cog.render()
    actual = cpp_mod.implementation_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_aligned_cog_source", actual)
    assert actual == test_helpers.load_expected("expected_aligned_cog_source")


def test_aligned_dial_header(consumer_cog_ir: cog.Cog) -> None:
    """Golden file test: dial header with mixed regular and nested aligned input struct."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/composition/tests/support/aligned_consumer_cog_dial.hh")
    dial = cppdial.Dial(
        cog_ir=consumer_cog_ir,
        class_name="ConsumerCogDial",
        cpp_namespace="clockwork::testing::aligned_consumer",
        dial_header=dial_header,
    )
    cpp_mod = dial.render()
    actual = cpp_mod.header_chunk.render_str(render_includes=True).strip()

    test_helpers.write_expected("expected_aligned_dial_header", actual)
    assert actual == test_helpers.load_expected("expected_aligned_dial_header")


def test_aligned_sensor_override_reaches_cpp_codegen(consumer_cog_ir: cog.Cog) -> None:
    """Consumer view overrides determine the generated aligned input capacities."""
    dial_header = Header(CLK_REPO, "clockwork/dsl/composition/tests/support/aligned_consumer_cog_dial.hh")
    dial = cppdial.Dial(
        cog_ir=consumer_cog_ir,
        class_name="ConsumerCogDial",
        cpp_namespace="clockwork::testing::aligned_consumer",
        dial_header=dial_header,
    )
    dial_header_text = dial.render().header_chunk.render_str(render_includes=True)

    assert "20U, 1U, 1U, false, true, false>& get_sensor() const;" in dial_header_text

    cpp_cog = cppcog.Cog.make(
        cog_ir=consumer_cog_ir,
        class_name="ConsumerCog",
        dial_name=None,
        header_name="consumer_cog.hh",
        cpp_namespace="clockwork::testing::aligned_consumer",
        dial_header=dial_header,
    )
    policy_header_text = cpp_cog.render().header_chunk.render_str(render_includes=True)
    sensor_policy = policy_header_text.split("struct AlignedSensorPolicy", maxsplit=1)[1]
    sensor_policy = sensor_policy.split("struct AlignedCameraPolicy", maxsplit=1)[0]

    assert "static constexpr auto max_view_size = 20U;" in sensor_policy


def test_multiple_alignment_groups_reach_cpp_codegen(multiple_aligned_cog_ir: cog.Cog) -> None:
    """Each stale path reports the alignment input that must be committed."""
    cpp_cog = cppcog.Cog.make(
        cog_ir=multiple_aligned_cog_ir,
        class_name="MultipleAlignedConsumerCog",
        dial_name=None,
        header_name="multiple_aligned_cog.hh",
        cpp_namespace="clockwork::testing::multiple_aligned",
        dial_header=Header(CLK_REPO, "multiple_aligned_cog_dial.hh"),
    )
    cpp_mod = cpp_cog.render()
    policy_header = cpp_mod.header_chunk.render_str(render_includes=True)
    policy_source = cpp_mod.implementation_chunk.render_str(render_includes=True)

    assert "static constexpr size_t alignment_index" not in policy_header
    assert "::jewels::Out<size_t> stale_alignment_index_out" in policy_header
    assert policy_source.count("*stale_alignment_index_out = 0;") == 4
    assert policy_source.count("*stale_alignment_index_out = 5;") == 1


def test_combined_alignment_groups_use_latest_available_message(combined_aligned_cog_ir: cog.Cog) -> None:
    """Alignment resolution selects the latest message at or after each input cursor."""
    cpp_cog = cppcog.Cog.make(
        cog_ir=combined_aligned_cog_ir,
        class_name="CombinedAlignedConsumerCog",
        dial_name=None,
        header_name="combined_aligned_cog.hh",
        cpp_namespace="clockwork::testing::combined_aligned",
        dial_header=Header(CLK_REPO, "combined_aligned_cog_dial.hh"),
    )
    policy_source = cpp_cog.render().implementation_chunk.render_str(render_includes=True)

    assert policy_source.count(".get_cursor_view();") == 4
    assert policy_source.count(".back();") == 4
    assert ".get_latest_msg();" not in policy_source


def test_permitted_empty_alignment_group_has_explicit_empty_path(empty_aligned_cog_ir: cog.Cog) -> None:
    """A permitted empty group receives empty aligned dials and is skipped during commit."""
    cpp_cog = cppcog.Cog.make(
        cog_ir=empty_aligned_cog_ir,
        class_name="EmptyAlignedConsumerCog",
        dial_name=None,
        header_name="empty_aligned_cog.hh",
        cpp_namespace="clockwork::testing::empty_aligned",
        dial_header=Header(CLK_REPO, "empty_aligned_cog_dial.hh"),
    )
    policy_source = cpp_cog.render().implementation_chunk.render_str(render_includes=True)

    assert "const auto alignment_aligned_candidates = ::std::get<1>(inputs).get_cursor_view();" in policy_source
    assert "if (alignment_aligned_candidates.empty())" in policy_source
    assert "::std::get<2>(inputs) = cog_inputs.template prepare_empty_aligned_input<2>();" in policy_source
    assert "::std::get<5>(inputs) = cog_inputs.template prepare_empty_aligned_input<5>();" in policy_source
    assert "if (!alignment_aligned_candidates.empty())" in policy_source
    assert policy_source.count("alignment_aligned_candidates.back();") == 2
