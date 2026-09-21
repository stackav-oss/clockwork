# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for journal instance discovery validation and resolution."""

from __future__ import annotations

import json
import os
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import cast

import pytest
from clockwork.tools.journal_file_generator import instance_discovery
from clockwork.tools.journal_file_generator.instance_discovery import (
    DiscoveryError,
    DiscoveryValidationError,
    create_instance_discovery_request,
    discover_cog_channel_maps,
    discover_cog_instances,
    expand_box_instance_paths,
    format_discovery_candidates,
)

_MULTI_ROUTE_SYSTEM_CLK = Path("clockwork/tests/support/test_system_multi_route.clk")
_MULTI_NODE_SYSTEM_CLK = Path("clockwork/tests/support/test_system_multi_node.clk")
_EMPTY_SYSTEM_CLK = Path("clockwork/tests/support/test_system_description_2.clk")
_SIGNAL_TEST_SYSTEM_CLK = Path("clockwork/tests/support/signal_test_system.clk")
_SIGNAL_TEST_BOX_CLK = Path("clockwork/tests/support/signal_test_box.clk")
_SNAPSHOT_TEST_SYSTEM_CLK = Path("clockwork/tests/support/snapshot_test_system.clk")
_ALIGNED_TEST_SYSTEM_CLK = Path("clockwork/dsl/composition/tests/support/aligned_consumer_system.clk")
_MULTI_ROUTE_CLK_TARGET = "@clockwork//clockwork/tests/support:test_system_multi_route_clk"
_MULTI_ROUTE_CLK_SOURCE_LABEL = "@clockwork//clockwork/tests/support:test_system_multi_route.clk"
_MULTI_ROUTE_TOPOLOGY_SUMMARY_TARGET = (
    "@clockwork//clockwork/tools/topology/tests:test_system_multi_route_topology_summary"
)
_MULTI_ROUTE_TOP_BOX = "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route"
_EMPTY_TOP_BOX = "@clockwork::clockwork::tests::support::test_system_description_2.test_system_2"
_MULTI_ROUTE_SOURCE_COG = f"{_MULTI_ROUTE_TOP_BOX}.source_box.source_cog"
_MULTI_ROUTE_SINK_COG_1 = (
    "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_1.sink_cog"
)
_MULTI_ROUTE_SINK_COG_2 = (
    "@clockwork::clockwork::tests::support::test_system_multi_route.test_system_multi_route.sink_box_2.sink_cog"
)


def _journal_file_generator_binary() -> Path:
    runfiles_dir = os.getenv("RUNFILES_DIR")
    assert runfiles_dir is not None

    matches = sorted(Path(runfiles_dir).glob("*/clockwork/tools/journal_file_generator/journal_file_generator"))
    assert matches
    return matches[0]


def _make_bazel_rule(
    rule_class: str,
    *,
    label_attrs: dict[str, str] | None = None,
    srcs: tuple[str, ...] = (),
) -> ET.Element:
    rule = ET.Element("rule", {"class": rule_class})
    for attr_name, value in (label_attrs or {}).items():
        ET.SubElement(rule, "label", {"name": attr_name, "value": value})
    if srcs:
        srcs_list = ET.SubElement(rule, "list", {"name": "srcs"})
        for src in srcs:
            ET.SubElement(srcs_list, "label", {"value": src})
    return rule


def _install_bazel_query_fixture(monkeypatch: pytest.MonkeyPatch, rules: dict[str, ET.Element]) -> None:
    def query_rule(label: str) -> ET.Element:
        assert label in rules
        return rules[label]

    monkeypatch.setattr(instance_discovery, "_query_bazel_rule", query_rule)


def _install_bazel_build_fixture(monkeypatch: pytest.MonkeyPatch) -> list[str]:
    built_labels: list[str] = []

    def materialize_target(label: str) -> None:
        built_labels.append(label)

    monkeypatch.setattr(instance_discovery, "_materialize_bazel_target", materialize_target)
    return built_labels


def _assert_multi_route_sink_candidates(candidates: list[instance_discovery.CogInstanceCandidate]) -> None:
    assert [candidate.cog_instance_path for candidate in candidates] == [
        _MULTI_ROUTE_SINK_COG_1,
        _MULTI_ROUTE_SINK_COG_2,
    ]


def test_discovery_request_accepts_system_clk_file() -> None:
    """Verify list-instances accepts source-declaration filters with a concrete system file."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
        cog_name="sink_cog",
        output_format="json",
    )

    assert request.system_clk_file == _MULTI_ROUTE_SYSTEM_CLK
    assert request.system_target is None
    assert request.source_clk_file == _MULTI_ROUTE_SYSTEM_CLK
    assert request.box_name == "MultiRouteSinkBox"
    assert request.cog_name == "sink_cog"
    assert request.output_format == "json"


def test_discovery_request_accepts_system_target() -> None:
    """Verify list-instances accepts a Bazel target as the concrete system context."""
    request = create_instance_discovery_request(
        system_target=_MULTI_ROUTE_CLK_TARGET,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )

    assert request.system_clk_file is None
    assert request.system_target == _MULTI_ROUTE_CLK_TARGET
    assert request.source_clk_file == _MULTI_ROUTE_SYSTEM_CLK
    assert request.box_name == "MultiRouteSinkBox"
    assert request.cog_name is None
    assert request.output_format == "table"


def test_discovery_request_validation_rejects_invalid_filters() -> None:
    """Verify list-instances option validation rejects incomplete or unsupported forms."""
    with pytest.raises(DiscoveryValidationError, match="exactly one"):
        create_instance_discovery_request(
            source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            box_name="MultiRouteSinkBox",
        )

    with pytest.raises(DiscoveryValidationError, match="exactly one"):
        create_instance_discovery_request(
            system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            system_target="//clockwork/tests/support:test_system_multi_route_topology_summary",
            source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            box_name="MultiRouteSinkBox",
        )

    with pytest.raises(DiscoveryValidationError, match="source-clk-file"):
        create_instance_discovery_request(
            system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            box_name="MultiRouteSinkBox",
        )

    with pytest.raises(DiscoveryValidationError, match="box-name"):
        create_instance_discovery_request(
            system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        )

    with pytest.raises(DiscoveryValidationError, match="format"):
        create_instance_discovery_request(
            system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            box_name="MultiRouteSinkBox",
            output_format="yaml",
        )


def test_discovery_finds_repeated_box_candidates() -> None:
    """Verify discovery returns one runtime cog instance per repeated box instance."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )

    candidates = discover_cog_instances(request)

    assert [candidate.cog_instance_path for candidate in candidates] == [
        _MULTI_ROUTE_SINK_COG_1,
        _MULTI_ROUTE_SINK_COG_2,
    ]
    assert [candidate.box_instance_path for candidate in candidates] == [
        _MULTI_ROUTE_SINK_COG_1.rsplit(".", maxsplit=1)[0],
        _MULTI_ROUTE_SINK_COG_2.rsplit(".", maxsplit=1)[0],
    ]
    assert {candidate.declared_box_name for candidate in candidates} == {"MultiRouteSinkBox"}
    assert {candidate.declared_cog_name for candidate in candidates} == {"sink_cog"}


def test_discovery_resolves_clk_system_target(monkeypatch: pytest.MonkeyPatch) -> None:
    """Verify discovery resolves a Bazel clk target to its source CLK file."""
    built_labels = _install_bazel_build_fixture(monkeypatch)
    _install_bazel_query_fixture(
        monkeypatch,
        {
            _MULTI_ROUTE_CLK_TARGET: _make_bazel_rule(
                "_clk",
                srcs=(_MULTI_ROUTE_CLK_SOURCE_LABEL,),
            )
        },
    )
    request = create_instance_discovery_request(
        system_target=_MULTI_ROUTE_CLK_TARGET,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )

    candidates = discover_cog_instances(request)

    _assert_multi_route_sink_candidates(candidates)
    assert built_labels == [_MULTI_ROUTE_CLK_TARGET]


def test_discovery_resolves_clk_source_label_system_target(monkeypatch: pytest.MonkeyPatch) -> None:
    """Verify discovery materializes the conventional clk target for a CLK source label."""
    built_labels = _install_bazel_build_fixture(monkeypatch)
    _install_bazel_query_fixture(
        monkeypatch,
        {
            _MULTI_ROUTE_CLK_TARGET: _make_bazel_rule(
                "_clk",
                srcs=(_MULTI_ROUTE_CLK_SOURCE_LABEL,),
            )
        },
    )
    request = create_instance_discovery_request(
        system_target=_MULTI_ROUTE_CLK_SOURCE_LABEL,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )

    candidates = discover_cog_instances(request)

    _assert_multi_route_sink_candidates(candidates)
    assert built_labels == [_MULTI_ROUTE_CLK_TARGET]


def test_discovery_resolves_topology_summary_system_target(monkeypatch: pytest.MonkeyPatch) -> None:
    """Verify discovery follows topology_summary targets to their underlying clk target."""
    built_labels = _install_bazel_build_fixture(monkeypatch)
    _install_bazel_query_fixture(
        monkeypatch,
        {
            _MULTI_ROUTE_TOPOLOGY_SUMMARY_TARGET: _make_bazel_rule(
                "topology_summary",
                label_attrs={"system_target": _MULTI_ROUTE_CLK_TARGET},
            ),
            _MULTI_ROUTE_CLK_TARGET: _make_bazel_rule(
                "_clk",
                srcs=(_MULTI_ROUTE_CLK_SOURCE_LABEL,),
            ),
        },
    )
    request = create_instance_discovery_request(
        system_target=_MULTI_ROUTE_TOPOLOGY_SUMMARY_TARGET,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )

    candidates = discover_cog_instances(request)

    _assert_multi_route_sink_candidates(candidates)
    assert built_labels == [_MULTI_ROUTE_CLK_TARGET]


def test_discovery_filters_declared_cog_inside_repeated_box() -> None:
    """Verify an explicit declared cog filter keeps the matching repeated runtime instances."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
        cog_name="sink_cog",
    )

    candidates = discover_cog_instances(request)

    assert [candidate.cog_instance_path for candidate in candidates] == [
        _MULTI_ROUTE_SINK_COG_1,
        _MULTI_ROUTE_SINK_COG_2,
    ]


def test_discovery_finds_candidates_when_source_clk_differs_from_system_clk() -> None:
    """Verify discovery matches imported source boxes against a separate system CLK file."""
    request = create_instance_discovery_request(
        system_clk_file=_SIGNAL_TEST_SYSTEM_CLK,
        source_clk_file=_SIGNAL_TEST_BOX_CLK,
        box_name="SignalTestBox",
        cog_name="signal_cog",
    )

    candidates = discover_cog_instances(request)

    assert len(candidates) == 1
    assert candidates[0].declared_box_name == "SignalTestBox"
    assert candidates[0].declared_cog_name == "signal_cog"
    assert candidates[0].box_instance_path.endswith(".box")
    assert candidates[0].cog_instance_path.endswith(".box.signal_cog")


def test_discovery_finds_cog_channel_maps_from_system_context() -> None:
    """Verify compiled system context maps cog members to channels."""
    channel_maps = discover_cog_channel_maps(system_clk_file=_SIGNAL_TEST_SYSTEM_CLK)
    signal_cog_path = "@clockwork::clockwork::tests::support::signal_test_system.signal_test_system.box.signal_cog"
    tester_cog_path = (
        "@clockwork::clockwork::tests::support::signal_test_system.signal_test_system.box.signal_tester_cog"
    )

    assert channel_maps[signal_cog_path].input_channels[0].cog_member_name == "trigger"
    assert channel_maps[signal_cog_path].input_channels[0].channel_name == "TriggerChannel"
    assert channel_maps[signal_cog_path].output_channels[0].cog_member_name == "ack"
    assert channel_maps[signal_cog_path].output_channels[0].channel_name == "AckChannel"
    assert channel_maps[tester_cog_path].input_channels[0].cog_member_name == "ack"
    assert channel_maps[tester_cog_path].input_channels[0].channel_name == "AckChannel"
    assert channel_maps[tester_cog_path].output_channels[0].cog_member_name == "trigger"
    assert channel_maps[tester_cog_path].output_channels[0].channel_name == "TriggerChannel"


def test_discovery_finds_snapshot_channel_maps_from_system_context() -> None:
    """Verify compiled snapshot policy metadata maps state endpoints to snapshot channels."""
    channel_maps = discover_cog_channel_maps(system_clk_file=_SNAPSHOT_TEST_SYSTEM_CLK)
    stateful_cog_path = (
        "@clockwork::clockwork::tests::support::snapshot_test_system.snapshot_test_system.box.stateful_cog"
    )

    snapshot_refs = channel_maps[stateful_cog_path].snapshot_channels

    assert channel_maps[stateful_cog_path].has_clockwork_state
    assert [(ref.cog_member_name, ref.channel_name) for ref in snapshot_refs] == [
        ("state", "StateSnapshotChannel"),
    ]


def test_discovery_preserves_aligned_input_member_names() -> None:
    """Verify the shared projection keeps aligned input names used by cog metrics."""
    channel_maps = discover_cog_channel_maps(system_clk_file=_ALIGNED_TEST_SYSTEM_CLK)
    consumer_path = (
        "@clockwork::clockwork::dsl::composition::tests::support::aligned_consumer_system."
        "aligned_consumer_system.box.consumer"
    )

    assert [(ref.cog_member_name, ref.channel_name) for ref in channel_maps[consumer_path].input_channels] == [
        ("aligned", "AlignmentChannel"),
        ("camera", "CameraChannel"),
        ("lidar", "LidarChannel"),
        ("radar", "RadarChannel"),
        ("raw_data", "RawDataChannel"),
        ("sensor", "SensorChannel"),
    ]


def test_box_scope_expands_nested_and_overlapping_box_paths() -> None:
    """Verify box-scope expansion recursively collects and de-duplicates cogs."""
    expanded = expand_box_instance_paths(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_instance_paths=(
            _MULTI_ROUTE_SINK_COG_1.rsplit(".", maxsplit=1)[0],
            _MULTI_ROUTE_TOP_BOX,
            _MULTI_ROUTE_SINK_COG_1.rsplit(".", maxsplit=1)[0],
        ),
    )

    assert expanded == tuple(sorted((_MULTI_ROUTE_SOURCE_COG, _MULTI_ROUTE_SINK_COG_1, _MULTI_ROUTE_SINK_COG_2)))


def test_box_scope_reports_unknown_and_empty_boxes() -> None:
    """Verify box-scope expansion rejects boxes it cannot turn into cog scope."""
    with pytest.raises(DiscoveryError, match="was not found"):
        expand_box_instance_paths(
            system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
            box_instance_paths=("missing.box",),
        )

    with pytest.raises(DiscoveryError, match="contains no runtime cog"):
        expand_box_instance_paths(
            system_clk_file=_EMPTY_SYSTEM_CLK,
            box_instance_paths=(_EMPTY_TOP_BOX,),
        )


def test_discovery_leaves_snapshot_refs_empty_without_snapshot_policies() -> None:
    """Verify cogs without snapshot policies still get channel maps without snapshot refs."""
    channel_maps = discover_cog_channel_maps(system_clk_file=_SIGNAL_TEST_SYSTEM_CLK)
    signal_cog_path = "@clockwork::clockwork::tests::support::signal_test_system.signal_test_system.box.signal_cog"

    assert not channel_maps[signal_cog_path].has_clockwork_state
    assert channel_maps[signal_cog_path].snapshot_channels == ()


def test_prefix_paths_from_anchor_include_bazel_outputs(tmp_path: Path) -> None:
    """Verify generated CLK files can be found through workspace Bazel output roots."""
    bazel_bin = tmp_path / "bazel-bin"
    bazel_out_bin = tmp_path / "bazel-out/k8-fastbuild/bin"
    bazel_bin.mkdir()
    bazel_out_bin.mkdir(parents=True)

    paths = instance_discovery._prefix_paths_from_anchor(tmp_path)

    assert bazel_bin in paths
    assert bazel_out_bin in paths


def test_discovery_reports_missing_source_clk_file() -> None:
    """Verify missing source files fail with a clear discovery error."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=Path("clockwork/tests/support/not_a_real_system.clk"),
        box_name="MultiRouteSinkBox",
    )

    with pytest.raises(DiscoveryError, match="Cannot load source CLK file"):
        discover_cog_instances(request)


def test_discovery_reports_missing_declared_box() -> None:
    """Verify source files must contain the requested declared box."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MissingBox",
    )

    with pytest.raises(DiscoveryError, match="Declared box 'MissingBox'"):
        discover_cog_instances(request)


def test_discovery_reports_missing_declared_cog() -> None:
    """Verify declared cog filters must name a cog inside the selected box."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
        cog_name="missing_cog",
    )

    with pytest.raises(DiscoveryError, match="Declared cog 'missing_cog'"):
        discover_cog_instances(request)


def test_discovery_reports_no_runtime_candidates() -> None:
    """Verify source boxes that are absent from the system report no runtime candidates."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_NODE_SYSTEM_CLK,
        box_name="TestSystemCogsBox2",
    )

    with pytest.raises(DiscoveryError, match="No runtime box instances"):
        discover_cog_instances(request)


def test_discovery_json_output_shape() -> None:
    """Verify JSON output is deterministic and machine-readable."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )
    candidates = discover_cog_instances(request)

    parsed = cast("list[dict[str, str]]", json.loads(format_discovery_candidates(candidates, "json")))

    assert parsed == [
        {
            "box_instance_path": _MULTI_ROUTE_SINK_COG_1.rsplit(".", maxsplit=1)[0],
            "cog_instance_path": _MULTI_ROUTE_SINK_COG_1,
            "declared_box": "MultiRouteSinkBox",
            "declared_cog": "sink_cog",
        },
        {
            "box_instance_path": _MULTI_ROUTE_SINK_COG_2.rsplit(".", maxsplit=1)[0],
            "cog_instance_path": _MULTI_ROUTE_SINK_COG_2,
            "declared_box": "MultiRouteSinkBox",
            "declared_cog": "sink_cog",
        },
    ]


def test_discovery_table_output_shape() -> None:
    """Verify table output includes the expected columns and candidates."""
    request = create_instance_discovery_request(
        system_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        source_clk_file=_MULTI_ROUTE_SYSTEM_CLK,
        box_name="MultiRouteSinkBox",
    )
    candidates = discover_cog_instances(request)

    table = format_discovery_candidates(candidates, "table")

    assert table.splitlines()[0].split() == [
        "box_instance_path",
        "cog_instance_path",
        "declared_box",
        "declared_cog",
    ]
    assert _MULTI_ROUTE_SINK_COG_1 in table
    assert _MULTI_ROUTE_SINK_COG_2 in table
    assert "MultiRouteSinkBox" in table


def test_cli_list_instances_prints_json() -> None:
    """Verify the list-instances CLI command prints discovered runtime paths."""
    result = subprocess.run(
        [
            str(_journal_file_generator_binary()),
            "list-instances",
            "--system-clk-file",
            str(_MULTI_ROUTE_SYSTEM_CLK),
            "--source-clk-file",
            str(_MULTI_ROUTE_SYSTEM_CLK),
            "--box-name",
            "MultiRouteSinkBox",
            "--format",
            "json",
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    parsed = cast("list[dict[str, str]]", json.loads(result.stdout))
    assert [item["cog_instance_path"] for item in parsed] == [_MULTI_ROUTE_SINK_COG_1, _MULTI_ROUTE_SINK_COG_2]
