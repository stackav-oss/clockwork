# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Static HTML renderer for journal reports."""

from __future__ import annotations

import html
import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Final, final

from clockwork.tools.journal_report.view_model import (
    AlignedInputModel,
    ChannelFlowEdgeModel,
    ChannelFlowNodeModel,
    ChannelMessageModel,
    ChannelModel,
    CogModel,
    ExecutionModel,
    MemoryTimelineModel,
    MemoryTimelinePointModel,
    MemoryTimelineSeriesModel,
    MessageExecutionLinkModel,
    OutputModel,
    ReportModel,
    TimelineBarModel,
    TimelineRowModel,
    build_report_model,
    report_model_to_dict,
)

_ASSET_DIR: Final = Path(__file__).with_name("assets")
_PACKAGE_DIR: Final = Path(__file__).parent
_DEFAULT_TITLE: Final = "Journal Report"
_LANDING_SECTION_ID: Final = "overview"
_SECTION_TABS: Final = (
    ("overview", "Overview"),
    ("execution-timeline", "Execution Timeline"),
    ("execution-details", "Execution Details"),
    ("channel-flow", "Channel Flow"),
    ("message-sequences", "Message Sequences"),
    ("readiness", "Replay Readiness"),
    ("anomalies", "Anomaly Highlights"),
    ("state-snapshots", "State Snapshots"),
)
_TIMELINE_MIN_WIDTH: Final = 1120
_TIMELINE_LABEL_WIDTH: Final = 300
_TIMELINE_RIGHT_PADDING: Final = 18
_TIMELINE_TOP_PADDING: Final = 34
_TIMELINE_BOTTOM_PADDING: Final = 24
_TIMELINE_ROW_HEIGHT: Final = 52
_TIMELINE_BAR_HEIGHT: Final = 12
_TIMELINE_LABEL_MAX_LENGTH: Final = 30
_TIMELINE_PIXELS_PER_EXECUTION: Final = 4
_TIMELINE_TICK_COUNT: Final = 5
_NANOSECONDS_PER_SECOND: Final = 1_000_000_000
_FLOW_LAYER_WIDTH: Final = 430
_FLOW_ROW_HEIGHT: Final = 74
_FLOW_NODE_WIDTH: Final = 250
_FLOW_NODE_HEIGHT: Final = 34
_FLOW_MARGIN_X: Final = 20
_FLOW_MARGIN_Y: Final = 28
_FLOW_EDGE_TITLE_CHANNEL_LIMIT: Final = 5
_MEMORY_GRAPH_WIDTH: Final = 840
_MEMORY_GRAPH_HEIGHT: Final = 220
_MEMORY_GRAPH_LEFT_PADDING: Final = 74
_MEMORY_GRAPH_RIGHT_PADDING: Final = 20
_MEMORY_GRAPH_TOP_PADDING: Final = 26
_MEMORY_GRAPH_BOTTOM_PADDING: Final = 42

if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from clockwork.journal import journal_pb2


@final
@dataclass(frozen=True, kw_only=True)
class _MemoryGraphGeometry:
    """Normalized dimensions and values for one current-memory graph."""

    memory_timeline: MemoryTimelineModel
    plot_width: int
    plot_height: int
    max_current_allocated: int


def render_report(
    journal: journal_pb2.JournalFile,
    *,
    title: str | None = None,
    message_data_api: str | None = None,
) -> str:
    """Render a complete self-contained HTML report."""
    report_title = title or _DEFAULT_TITLE
    model = build_report_model(journal, title=report_title)
    escaped_title = html.escape(report_title)
    data_json = json_dumps_for_script(report_model_to_dict(model))
    css = _read_asset("report.css")
    javascript = _read_package_file("report.js")
    body = _render_body(model)
    message_data_attribute = (
        "" if message_data_api is None else f' data-message-data-api="{html.escape(message_data_api, quote=True)}"'
    )
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{escaped_title}</title>
  <style>{css}</style>
</head>
<body>
  <header class="report-header">
    <h1>{escaped_title}</h1>
  </header>
  <main id="journal-report-root" class="report-shell" data-report-ready="false"{message_data_attribute}>
{_render_section_tabs()}
{body}
  </main>
  <script id="journal-report-data" type="application/json">{data_json}</script>
  <script>{javascript}</script>
</body>
</html>
"""


def json_dumps_for_script(data: object) -> str:
    """Serialize JSON for safe embedding inside a script tag."""
    serialized = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return (
        serialized.replace("&", "\\u0026")
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
        .replace("\u2028", "\\u2028")
        .replace("\u2029", "\\u2029")
    )


def _render_section_tabs() -> str:
    links = "\n".join(_render_section_tab(section_id, label) for section_id, label in _SECTION_TABS)
    return f"""    <nav id="report-section-tabs" class="report-section-tabs" aria-label="Report sections">
{links}
    </nav>"""


def _render_section_tab(section_id: str, label: str) -> str:
    is_active = section_id == _LANDING_SECTION_ID
    class_name = "report-section-tab report-section-tab-active" if is_active else "report-section-tab"
    tab_index = 0 if is_active else -1
    return (
        f'      <a class="{class_name}" href="#{_escape(section_id)}" role="tab" '
        f'aria-selected="{str(is_active).lower()}" tabindex="{tab_index}">{_escape(label)}</a>'
    )


def _report_section_attributes(section_id: str) -> str:
    is_active = section_id == _LANDING_SECTION_ID
    class_name = "report-section report-section-active" if is_active else "report-section"
    hidden = "" if is_active else " hidden"
    return f'id="{_escape(section_id)}" class="{class_name}"{hidden}'


def _render_body(model: ReportModel) -> str:
    return "\n".join(
        [
            _render_overview(model),
            _render_timeline(model),
            _render_execution_details(model),
            _render_channel_flow(model),
            _render_message_sequences(model),
            _render_readiness(model),
            _render_anomalies(model),
            _render_state_snapshots(model),
        ]
    )


def _render_overview(model: ReportModel) -> str:
    cards = "\n".join(
        f"""      <div class="overview-card">
        <span>{_escape(card.label)}</span>
        <strong>{_escape(card.value)}</strong>
      </div>"""
        for card in model.overview_cards
    )
    metadata = model.metadata
    return f"""    <section {_report_section_attributes("overview")}>
      <h2>Overview</h2>
      <div class="overview-grid">
{cards}
      </div>
      {
        _definition_list(
            (
                (
                    "Time Range",
                    f"{_format_timestamp_ns(metadata.start_time_ns)} to {_format_timestamp_ns(metadata.end_time_ns)}",
                ),
                ("Unix Time Range (ns)", f"{metadata.start_time_ns} to {metadata.end_time_ns}"),
                ("Log URI", metadata.log_uri),
                ("Requested Cogs", ", ".join(metadata.requested_cog_instance_paths)),
                ("Generator", metadata.generator_version),
            )
        )
    }
    </section>"""


def _render_timeline(model: ReportModel) -> str:
    timeline_html = (
        _timeline_svg(model)
        if any(row.bars for row in model.timeline.rows)
        else '        <p class="empty-state">No executions in timeline.</p>'
    )
    return f"""    <section {_report_section_attributes("execution-timeline")}>
      <h2>Execution Timeline</h2>
      <div id="execution-timeline-root" class="timeline-root">
{timeline_html}
      </div>
    </section>"""


def _timeline_svg(model: ReportModel) -> str:
    timeline = model.timeline
    row_count = len(timeline.rows)
    height = _TIMELINE_TOP_PADDING + row_count * _TIMELINE_ROW_HEIGHT + _TIMELINE_BOTTOM_PADDING
    timeline_width = _timeline_width(timeline.rows)
    plot_width = timeline_width - _TIMELINE_LABEL_WIDTH - _TIMELINE_RIGHT_PADDING
    time_range = max(1, timeline.end_time_ns - timeline.start_time_ns)
    timeline_geometry = (timeline_width, plot_width)
    time_bounds = (timeline.start_time_ns, time_range)
    rows = "\n".join(
        _render_timeline_row(row, row_index, timeline_geometry, time_bounds)
        for row_index, row in enumerate(timeline.rows)
    )
    axis = _render_timeline_axis(timeline.start_time_ns, timeline.end_time_ns, timeline_width, plot_width, height)
    return f"""        <svg class="timeline-svg" width="{timeline_width}" height="{height}" viewBox="0 0 {timeline_width} {height}" role="img" aria-label="Execution timeline">
{axis}
{rows}
        </svg>"""


def _timeline_width(rows: Sequence[TimelineRowModel]) -> int:
    min_plot_width = _TIMELINE_MIN_WIDTH - _TIMELINE_LABEL_WIDTH - _TIMELINE_RIGHT_PADDING
    max_row_execution_count = max((len(row.bars) for row in rows), default=0)
    plot_width = max(min_plot_width, max_row_execution_count * _TIMELINE_PIXELS_PER_EXECUTION)
    return _TIMELINE_LABEL_WIDTH + _TIMELINE_RIGHT_PADDING + plot_width


def _render_timeline_axis(
    start_time_ns: int, end_time_ns: int, timeline_width: int, plot_width: int, height: int
) -> str:
    axis_y = _TIMELINE_TOP_PADDING - 14
    time_range = max(1, end_time_ns - start_time_ns)
    grid_bottom_y = height - _TIMELINE_BOTTOM_PADDING + 4
    ticks = "\n".join(
        _render_timeline_tick(tick_time_ns, (start_time_ns, time_range), plot_width, axis_y, grid_bottom_y)
        for tick_time_ns in _timeline_ticks(start_time_ns, end_time_ns)
    )
    return f"""          <line class="timeline-axis" x1="{_TIMELINE_LABEL_WIDTH}" x2="{timeline_width - _TIMELINE_RIGHT_PADDING}" y1="{axis_y}" y2="{axis_y}"></line>
{ticks}"""


def _render_timeline_tick(
    tick_time_ns: int,
    time_bounds: tuple[int, int],
    plot_width: int,
    axis_y: int,
    grid_bottom_y: int,
) -> str:
    start_time_ns, time_range = time_bounds
    x = _TIMELINE_LABEL_WIDTH + ((tick_time_ns - start_time_ns) / time_range) * plot_width
    return f"""          <line class="timeline-grid" x1="{x:.2f}" x2="{x:.2f}" y1="{axis_y}" y2="{grid_bottom_y}"></line>
          <text class="timeline-tick-label" x="{x:.2f}" y="{axis_y - 7}">{_escape(_format_timeline_offset_ns(tick_time_ns - start_time_ns))}</text>"""


def _timeline_ticks(start_time_ns: int, end_time_ns: int) -> tuple[int, ...]:
    time_range = max(1, end_time_ns - start_time_ns)
    return tuple(
        start_time_ns + round((time_range * index) / (_TIMELINE_TICK_COUNT - 1))
        for index in range(_TIMELINE_TICK_COUNT)
    )


def _render_timeline_row(
    row: TimelineRowModel,
    row_index: int,
    timeline_geometry: tuple[int, int],
    time_bounds: tuple[int, int],
) -> str:
    timeline_width = timeline_geometry[0]
    row_top_y = _TIMELINE_TOP_PADDING + row_index * _TIMELINE_ROW_HEIGHT
    row_y = row_top_y + (_TIMELINE_ROW_HEIGHT / 2)
    cog_instance_path = row.cog_instance_path
    bars = "\n".join(_render_timeline_bar(bar, row.cog_id, row_y, timeline_geometry, time_bounds) for bar in row.bars)
    background_class = "timeline-row-bg timeline-row-bg-even" if row_index % 2 == 0 else "timeline-row-bg"
    return f"""          <rect class="{background_class}" x="0" y="{row_top_y}" width="{timeline_width}" height="{_TIMELINE_ROW_HEIGHT}"></rect>
          <text class="timeline-label" x="0" y="{row_y:.2f}">{_escape(_timeline_label(cog_instance_path))}<title>{_escape(cog_instance_path)}</title></text>
          <text class="timeline-row-count" x="{_TIMELINE_LABEL_WIDTH - 14}" y="{row_y:.2f}">{_escape(_execution_count_label(len(row.bars)))}</text>
          <line class="timeline-lane" x1="{_TIMELINE_LABEL_WIDTH}" x2="{timeline_width - _TIMELINE_RIGHT_PADDING}" y1="{row_y:.2f}" y2="{row_y:.2f}"></line>
{bars}"""


def _render_timeline_bar(
    bar: TimelineBarModel,
    cog_id: str,
    row_y: float,
    timeline_geometry: tuple[int, int],
    time_bounds: tuple[int, int],
) -> str:
    timeline_width, plot_width = timeline_geometry
    start_time_ns, time_range = time_bounds
    bar_start_time_ns = bar.start_time_ns
    duration_ns = bar.duration_ns
    execution_id = bar.execution_id
    execution_index = bar.execution_index
    duration_bucket = bar.duration_bucket.value
    bar_end_time_ns = bar.end_time_ns
    x = _TIMELINE_LABEL_WIDTH + ((bar_start_time_ns - start_time_ns) / time_range) * plot_width
    width = max(1, (duration_ns / time_range) * plot_width)
    hit_x = max(_TIMELINE_LABEL_WIDTH, x - 4)
    hit_width = max(10, min(timeline_width - _TIMELINE_RIGHT_PADDING - hit_x, width + 8))
    bar_y = row_y - (_TIMELINE_BAR_HEIGHT / 2)
    title = f"Execution {execution_index}: start {bar_start_time_ns} ns, end {bar_end_time_ns} ns, duration {duration_ns} ns"
    return f"""          <a href="#{_escape(execution_id)}" aria-label="Execution {execution_index}">
            <title>{_escape(title)}</title>
            <rect class="timeline-bar-hit" x="{hit_x:.2f}" y="{bar_y - 6:.2f}" width="{hit_width:.2f}" height="{_TIMELINE_BAR_HEIGHT + 12}" rx="4" data-cog-id="{_escape(cog_id)}" data-execution-id="{_escape(execution_id)}" data-execution-index="{execution_index}" data-start-time-ns="{bar_start_time_ns}" data-end-time-ns="{bar_end_time_ns}" data-duration-ns="{duration_ns}"><title>{_escape(title)}</title></rect>
            <rect class="timeline-bar timeline-bar-{_escape(duration_bucket)}" x="{x:.2f}" y="{bar_y:.2f}" width="{width:.2f}" height="{_TIMELINE_BAR_HEIGHT}" rx="2" data-cog-id="{_escape(cog_id)}" data-execution-id="{_escape(execution_id)}" data-execution-index="{execution_index}" data-start-time-ns="{bar_start_time_ns}" data-end-time-ns="{bar_end_time_ns}" data-duration-ns="{duration_ns}"><title>{_escape(title)}</title></rect>
          </a>"""


def _render_execution_details(model: ReportModel) -> str:
    cog_links: list[str] = []
    cog_groups: list[str] = []
    for cog in model.cogs:
        executions = cog.executions
        if not executions:
            continue
        cog_id = cog.id
        cog_instance_path = cog.cog_instance_path
        group_id = f"{cog_id}-executions"
        cog_label = _short_cog_label(cog_instance_path)
        cog_links.append(_execution_cog_link(group_id, cog_instance_path, cog_label, len(executions)))
        detail_panels = "\n".join(_render_execution_detail(cog_instance_path, execution) for execution in executions)
        jump_controls = _render_execution_jump_controls(cog_id, cog_label)
        memory_graph = _render_cog_memory_graph(cog)
        cog_groups.append(
            f"""      <section id="{_escape(group_id)}" class="execution-cog-group">
        <h3>{_escape(cog_label)}</h3>
{_definition_list((("Cog", cog_instance_path), ("Executions", str(len(executions)))))}
{memory_graph}
{jump_controls}
{detail_panels}
      </section>"""
        )
    if not cog_groups:
        detail_html = '      <p class="empty-state">No executions.</p>'
    else:
        links_html = "\n".join(cog_links)
        groups_html = "\n".join(cog_groups)
        detail_html = f"""      <nav class="execution-cog-links" aria-label="Execution cogs">
{links_html}
      </nav>
{groups_html}"""
    return f"""    <section {_report_section_attributes("execution-details")}>
      <h2>Execution Details</h2>
{detail_html}
    </section>"""


def _execution_cog_link(group_id: str, cog_instance_path: str, cog_label: str, execution_count: int) -> str:
    return (
        f'        <a href="#{_escape(group_id)}" title="{_escape(cog_instance_path)}">'
        + f"{_escape(cog_label)} ({execution_count})</a>"
    )


def _render_execution_jump_controls(cog_id: str, cog_label: str) -> str:
    input_id = f"{cog_id}-execution-jump-input"
    button_id = f"{cog_id}-execution-jump-button"
    status_id = f"{cog_id}-execution-jump-status"
    return f"""        <div class="execution-jump-controls" data-cog-id="{_escape(cog_id)}" data-cog-label="{_escape(cog_label)}">
          <label class="execution-jump-label" for="{_escape(input_id)}">Timestamp</label>
          <input id="{_escape(input_id)}" class="execution-jump-input" type="text" inputmode="decimal" autocomplete="off">
          <button id="{_escape(button_id)}" class="execution-jump-button" type="button">Jump</button>
          <span id="{_escape(status_id)}" class="execution-jump-status" aria-live="polite"></span>
        </div>"""


def _render_execution_detail(cog_instance_path: str, execution: ExecutionModel) -> str:
    execution_id = execution["id"]
    execution_index = execution["execution_index"]
    execution_start_time_ns = execution["execution_start_time_ns"]
    timing = _definition_list(
        (
            ("Cog", cog_instance_path),
            ("Execution", str(execution_index)),
            ("Start Time (ns)", str(execution_start_time_ns)),
            ("Duration", f"{execution['execution_duration_ns']} ns"),
            ("Ready Latency", f"{execution['ready_to_exec_latency_ns']} ns"),
            ("Attempt Latency", f"{execution['attempt_to_exec_latency_ns']} ns"),
            ("Requeues", str(execution["requeue_count"])),
        )
    )
    return f"""      <details id="{_escape(execution_id)}" class="execution-detail">
        <summary>{_escape(_short_cog_label(cog_instance_path))} execution {execution_index} @ {_timestamp_time_with_unix_ns(execution_start_time_ns)}</summary>
{timing}
{_render_conditions(execution)}
{_render_input_views(execution)}
{_render_outputs(execution)}
{_render_memory_stats(execution)}
{_render_alignment(execution)}
      </details>"""


def _render_cog_memory_graph(cog: CogModel) -> str:
    memory_timeline = cog.memory_timeline
    if memory_timeline is None:
        return '        <p class="empty-state">No memory statistics for this cog.</p>'
    graphs = "\n".join(_render_memory_resource_graph(series, memory_timeline) for series in memory_timeline.series)
    return f"""        <div class="execution-memory-graph">
          <h4>Memory Usage Over Time</h4>
          <p class="memory-usage-caption">Current allocated bytes by resource.</p>
{graphs}
        </div>"""


def _render_memory_resource_graph(
    series: MemoryTimelineSeriesModel,
    memory_timeline: MemoryTimelineModel,
) -> str:
    graph_geometry = _MemoryGraphGeometry(
        memory_timeline=memory_timeline,
        plot_width=_MEMORY_GRAPH_WIDTH - _MEMORY_GRAPH_LEFT_PADDING - _MEMORY_GRAPH_RIGHT_PADDING,
        plot_height=_MEMORY_GRAPH_HEIGHT - _MEMORY_GRAPH_TOP_PADDING - _MEMORY_GRAPH_BOTTOM_PADDING,
        max_current_allocated=max(1, *(point.current_allocated for point in series.points)),
    )
    points = tuple(
        _memory_graph_point(point.time_ns, point.current_allocated, graph_geometry) for point in series.points
    )
    point_coordinates = " ".join(points)
    line = (
        f'                <polyline class="memory-usage-line" points="{point_coordinates}"></polyline>'
        if len(points) > 1
        else ""
    )
    circles = "\n".join(_render_memory_point(point, graph_geometry) for point in series.points)
    axis_y = _MEMORY_GRAPH_HEIGHT - _MEMORY_GRAPH_BOTTOM_PADDING
    return f"""          <section class="memory-usage-resource" data-resource-name="{_escape(series.resource_name)}">
            <h5>{_escape(series.resource_name)}</h5>
            <div class="memory-usage-graph-wrap">
              <svg class="memory-usage-graph" width="{_MEMORY_GRAPH_WIDTH}" height="{_MEMORY_GRAPH_HEIGHT}" viewBox="0 0 {_MEMORY_GRAPH_WIDTH} {_MEMORY_GRAPH_HEIGHT}" role="img" aria-label="Current allocated memory over time for {_escape(series.resource_name)}">
                <line class="memory-usage-axis" x1="{_MEMORY_GRAPH_LEFT_PADDING}" x2="{_MEMORY_GRAPH_WIDTH - _MEMORY_GRAPH_RIGHT_PADDING}" y1="{axis_y}" y2="{axis_y}"></line>
                <line class="memory-usage-axis" x1="{_MEMORY_GRAPH_LEFT_PADDING}" x2="{_MEMORY_GRAPH_LEFT_PADDING}" y1="{_MEMORY_GRAPH_TOP_PADDING}" y2="{axis_y}"></line>
                <text class="memory-usage-axis-label" x="{_MEMORY_GRAPH_LEFT_PADDING}" y="{_MEMORY_GRAPH_HEIGHT - 14}">+0 ns</text>
                <text class="memory-usage-axis-label" x="{_MEMORY_GRAPH_WIDTH - _MEMORY_GRAPH_RIGHT_PADDING}" y="{_MEMORY_GRAPH_HEIGHT - 14}" text-anchor="end">+{memory_timeline.end_time_ns - memory_timeline.start_time_ns} ns</text>
                <text class="memory-usage-axis-label" x="{_MEMORY_GRAPH_LEFT_PADDING - 8}" y="{_MEMORY_GRAPH_TOP_PADDING + 4}" text-anchor="end">{graph_geometry.max_current_allocated} B</text>
                <text class="memory-usage-axis-label" x="{_MEMORY_GRAPH_LEFT_PADDING - 8}" y="{axis_y}" text-anchor="end">0 B</text>
{line}
{circles}
              </svg>
            </div>
          </section>"""


def _memory_graph_point(
    time_ns: int,
    current_allocated: int,
    graph_geometry: _MemoryGraphGeometry,
) -> str:
    x, y = _memory_graph_coordinates(time_ns, current_allocated, graph_geometry)
    return f"{_snap_memory_graph_coordinate(x):.2f},{_snap_memory_graph_coordinate(y):.2f}"


def _render_memory_point(
    point: MemoryTimelinePointModel,
    graph_geometry: _MemoryGraphGeometry,
) -> str:
    point_time_ns = point.time_ns
    point_current_allocated = point.current_allocated
    x, y = _memory_graph_coordinates(point_time_ns, point_current_allocated, graph_geometry)
    x = _snap_memory_graph_coordinate(x)
    y = _snap_memory_graph_coordinate(y)
    title = f"Execution {point.execution_index}: current allocated {point_current_allocated} bytes"
    return f"""                <a href="#{_escape(point.execution_id)}">
                  <circle class="memory-usage-point" cx="{x:.2f}" cy="{y:.2f}" r="4"><title>{_escape(title)}</title></circle>
                </a>"""


def _memory_graph_coordinates(
    time_ns: int,
    current_allocated: int,
    graph_geometry: _MemoryGraphGeometry,
) -> tuple[float, float]:
    memory_timeline = graph_geometry.memory_timeline
    time_range_ns = max(1, memory_timeline.end_time_ns - memory_timeline.start_time_ns)
    x = (
        _MEMORY_GRAPH_LEFT_PADDING
        + ((time_ns - memory_timeline.start_time_ns) / time_range_ns) * graph_geometry.plot_width
    )
    y = (
        _MEMORY_GRAPH_TOP_PADDING
        + (1 - max(0, current_allocated) / graph_geometry.max_current_allocated) * graph_geometry.plot_height
    )
    return x, y


def _snap_memory_graph_coordinate(value: float) -> float:
    return float(round(value))


def _render_channel_flow(model: ReportModel) -> str:
    if not model.channel_flow.nodes:
        flow_html = '      <p class="empty-state">No channel flow data.</p>'
    else:
        flow_html = f"""{_channel_flow_svg(model)}
{_channel_flow_edge_table(model)}"""
    return f"""    <section {_report_section_attributes("channel-flow")}>
      <h2>Channel Flow</h2>
{flow_html}
    </section>"""


def _channel_flow_svg(model: ReportModel) -> str:
    positions = {
        node.id: (
            _FLOW_MARGIN_X + node.layer * _FLOW_LAYER_WIDTH,
            _FLOW_MARGIN_Y + node.row * _FLOW_ROW_HEIGHT,
        )
        for node in model.channel_flow.nodes
    }
    width = max(360, max(x for x, _ in positions.values()) + _FLOW_NODE_WIDTH + _FLOW_MARGIN_X)
    height = max(140, max(y for _, y in positions.values()) + _FLOW_NODE_HEIGHT + _FLOW_MARGIN_Y)
    edges = "\n".join(
        _render_channel_flow_edge_group(source_id, target_id, group_edges, positions)
        for source_id, target_id, group_edges in _channel_flow_edge_groups(model.channel_flow.edges)
    )
    nodes = "\n".join(_render_channel_flow_node(node, positions[node.id]) for node in model.channel_flow.nodes)
    return f"""      <div class="channel-flow-wrap">
        <svg class="channel-flow-svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="Channel flow diagram">
          <defs>
            <marker id="flow-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
              <path d="M 0 0 L 10 5 L 0 10 z"></path>
            </marker>
          </defs>
{edges}
{nodes}
        </svg>
      </div>"""


def _render_channel_flow_node(node: ChannelFlowNodeModel, position: tuple[int, int]) -> str:
    x, y = position
    class_name = "flow-node flow-node-unknown" if node.is_unknown else "flow-node"
    label = _channel_flow_display_label(node.label)
    return f"""          <g id="{_escape(node.id)}" class="{class_name}">
            <rect x="{x}" y="{y}" width="{_FLOW_NODE_WIDTH}" height="{_FLOW_NODE_HEIGHT}" rx="4"></rect>
            <text x="{x + 10}" y="{y + 22}">{_escape(_short_label(label, 28))}<title>{_escape(node.label)}</title></text>
          </g>"""


def _channel_flow_edge_groups(
    edges: Sequence[ChannelFlowEdgeModel],
) -> tuple[tuple[str, str, tuple[ChannelFlowEdgeModel, ...]], ...]:
    grouped_edges: dict[tuple[str, str], list[ChannelFlowEdgeModel]] = {}
    for edge in edges:
        grouped_edges.setdefault((edge.source_node_id, edge.target_node_id), []).append(edge)
    return tuple(
        (source_id, target_id, tuple(sorted(group, key=lambda edge: edge.channel_name)))
        for (source_id, target_id), group in sorted(grouped_edges.items())
    )


def _render_channel_flow_edge_group(
    source_node_id: str,
    target_node_id: str,
    edges: Sequence[ChannelFlowEdgeModel],
    positions: dict[str, tuple[int, int]],
) -> str:
    source_x, source_y = positions[source_node_id]
    target_x, target_y = positions[target_node_id]
    x1 = source_x + _FLOW_NODE_WIDTH
    y1 = source_y + (_FLOW_NODE_HEIGHT // 2)
    x2 = target_x
    y2 = target_y + (_FLOW_NODE_HEIGHT // 2)
    label_x = (x1 + x2) / 2
    if x2 < x1:
        control_y = max(y1, y2) + 36
        edge_shape = (
            f'<path class="flow-edge" d="M {x1} {y1} C {label_x:.2f} {control_y}, '
            f'{label_x:.2f} {control_y}, {x2} {y2}" marker-end="url(#flow-arrow)"></path>'
        )
        label_y = control_y + 6
    else:
        edge_shape = f'<path class="flow-edge" d="M {x1} {y1} L {x2} {y2}" marker-end="url(#flow-arrow)"></path>'
        label_y = min(y1, y2) - 8
    edge_label = _channel_flow_edge_group_label(edges)
    title = _channel_flow_edge_group_title(edges)
    line = f"""            <title>{_escape(title)}</title>
            {edge_shape}
            <text class="flow-edge-label" x="{label_x:.2f}" y="{label_y:.2f}">{_escape(edge_label)}</text>"""
    if len(edges) == 1:
        href = f"#message-sequence-{edges[0].channel_id}"
        return f"""          <a href="{_escape(href)}" class="flow-edge-link" aria-label="{_escape(title)}">
{line}
          </a>"""
    return f"""          <g class="flow-edge-group" aria-label="{_escape(title)}">
{line}
          </g>"""


def _channel_flow_edge_group_label(edges: Sequence[ChannelFlowEdgeModel]) -> str:
    channel_count = len(edges)
    message_count = sum(edge.message_count for edge in edges)
    channel_noun = "channel" if channel_count == 1 else "channels"
    message_noun = "msg" if message_count == 1 else "msgs"
    return f"{channel_count} {channel_noun}, {message_count:,} {message_noun}"


def _channel_flow_edge_group_title(edges: Sequence[ChannelFlowEdgeModel]) -> str:
    if len(edges) == 1:
        edge = edges[0]
        return f"{edge.channel_name}: {edge.label}"
    channel_names = tuple(edge.channel_name for edge in edges)
    listed_channels = ", ".join(channel_names[:_FLOW_EDGE_TITLE_CHANNEL_LIMIT])
    if len(channel_names) > _FLOW_EDGE_TITLE_CHANNEL_LIMIT:
        listed_channels = f"{listed_channels}, and {len(channel_names) - _FLOW_EDGE_TITLE_CHANNEL_LIMIT} more"
    return f"{_channel_flow_edge_group_label(edges)}: {listed_channels}"


def _channel_flow_edge_table(model: ReportModel) -> str:
    node_labels = {node.id: node.label for node in model.channel_flow.nodes}
    rows = tuple(
        (
            f'<a href="#message-sequence-{_escape(edge.channel_id)}">{_escape(edge.channel_name)}</a>',
            _channel_flow_label_html(node_labels.get(edge.source_node_id, edge.source_node_id)),
            _channel_flow_label_html(node_labels.get(edge.target_node_id, edge.target_node_id)),
            _escape(edge.label),
        )
        for edge in model.channel_flow.edges
    )
    return f"""      <h3 class="channel-flow-edge-heading">Channel Edges</h3>
{_table(("Channel", "Source", "Sink", "Messages"), rows, empty_message="No channel edges.", escape_cells=False)}"""


def _render_message_sequences(model: ReportModel) -> str:
    if not model.channels:
        sequence_html = '      <p class="empty-state">No channel summaries.</p>'
    else:
        sequence_html = "\n".join(_render_message_sequence(channel) for channel in model.channels)
    return f"""    <section {_report_section_attributes("message-sequences")}>
      <h2>Message Sequences</h2>
{sequence_html}
    </section>"""


def _render_message_sequence(channel: ChannelModel) -> str:
    messages = channel["messages"]
    if not messages:
        return _render_message_sequence_placeholder(channel)
    return _render_message_sequence_table(channel, messages)


def _render_message_sequence_placeholder(channel: ChannelModel) -> str:
    channel_id = channel["id"]
    channel_name = channel["channel_name"]
    consumers = ", ".join(channel["consumer_cog_instances"]) or "none"
    # fmt: off
    metadata = _definition_list(
        (
            ("Producer", channel["producer_cog_instance"] or "unknown"),
            ("Consumers", consumers),
            ("Messages", str(channel["message_count"])),
            ("Sequence Range", f"{channel['first_sequence_number']} to {channel['last_sequence_number']}"),
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            ("Logged", "yes" if bool(channel["has_logged_messages"]) else "no"),
        )
    )
    # fmt: on
    return f"""      <article id="message-sequence-{_escape(channel_id)}" class="message-sequence-summary">
        <h3>{_escape(channel_name)}</h3>
{metadata}
        <p class="empty-state">Per-message detail is not present in this journal; showing channel summary only.</p>
      </article>"""


def _render_message_sequence_table(
    channel: ChannelModel,
    messages: Sequence[ChannelMessageModel],
) -> str:
    channel_id = channel["id"]
    channel_name = channel["channel_name"]
    consumers = ", ".join(channel["consumer_cog_instances"]) or "none"
    # fmt: off
    metadata = _definition_list(
        (
            ("Producer", channel["producer_cog_instance"] or "unknown"),
            ("Consumers", consumers),
            ("Messages", str(channel["message_count"])),
            ("Sequence Range", f"{channel['first_sequence_number']} to {channel['last_sequence_number']}"),
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            ("Logged", "yes" if bool(channel["has_logged_messages"]) else "no"),
        )
    )
    # fmt: on
    return f"""      <article id="message-sequence-{_escape(channel_id)}" class="message-sequence-summary">
        <h3>{_escape(channel_name)}</h3>
{metadata}
{_message_sequence_table(messages)}
      </article>"""


def _message_sequence_table(messages: Sequence[ChannelMessageModel]) -> str:
    rows = "\n".join(_message_sequence_row(message) for message in messages)
    headers = (
        "<th>Sequence</th><th>Publish Time</th><th>Payload Bytes</th>"
        "<th>Producer Executions</th><th>Consumer Executions</th>"
    )
    return f"""      <div class="table-wrap">
        <table>
          <thead><tr>{headers}</tr></thead>
          <tbody>
{rows}
          </tbody>
        </table>
      </div>"""


def _message_sequence_row(message: ChannelMessageModel) -> str:
    class_name = ' class="message-row-unconsumed"' if message.is_produced_unconsumed else ""
    consumers = _message_consumers_html(message)
    return f"""        <tr id="{_escape(message.id)}"{class_name}>
          <td>{message.sequence_number}</td>
          <td>{_timestamp_time_with_unix_ns(message.publish_time_ns)}</td>
          <td>{message.payload_size_bytes}</td>
          <td>{_execution_links_html(message.producer_links)}</td>
          <td>{consumers}</td>
        </tr>"""


def _message_consumers_html(message: ChannelMessageModel) -> str:
    if message.consumer_links:
        return _execution_links_html(message.consumer_links)
    if message.producer_links:
        return '<span class="message-unconsumed">not consumed</span>'
    return "none"


def _execution_links_html(links: Sequence[MessageExecutionLinkModel]) -> str:
    if not links:
        return "none"
    return ", ".join(
        f'<a href="#{_escape(link.execution_id)}" title="{_escape(link.cog_instance_path)}">{_escape(link.label)}</a>'
        for link in links
    )


def _render_conditions(execution: ExecutionModel) -> str:
    conditions = execution["conditions"]
    rows = tuple((condition.name, "yes" if condition.value else "no") for condition in conditions)
    return _detail_block(
        "Conditions",
        _table(("Name", "Value"), rows, empty_message="No conditions."),
    )


def _render_input_views(execution: ExecutionModel) -> str:
    input_views = execution["input_views"]
    # fmt: off
    rows = tuple(
        (
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            _escape(str(input_view["cog_member_name"])),
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            _escape(str(input_view["channel_name"])),
            _escape(str(input_view["cursor_sequence_number"])),
            _sequence_numbers_html(
                input_view["visible_sequence_numbers"],
                first_new_index=input_view["first_new_index"],
                message_ids_by_sequence=input_view["visible_sequence_message_ids"],
                channel_name=input_view["channel_name"],
            ),
            _sequence_numbers_html(
                input_view["new_sequence_numbers"],
                first_new_index=0,
                message_ids_by_sequence=input_view["visible_sequence_message_ids"],
                channel_name=input_view["channel_name"],
            ),
            "unavailable",
        )
        for input_view in input_views
    )
    # fmt: on
    return _detail_block(
        "Inputs",
        _table(
            ("Input", "Channel", "Cursor", "Visible Sequences", "New Sequences", "Staleness"),
            rows,
            empty_message="No input views.",
            escape_cells=False,
        ),
    )


def _render_outputs(execution: ExecutionModel) -> str:
    outputs = execution["outputs"]
    # fmt: off
    rows = tuple(
        (
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            _escape(str(output["channel_name"])),
            _output_sequence_numbers_html(output),
        )
        for output in outputs
    )
    # fmt: on
    return _detail_block(
        "Outputs",
        _table(("Channel", "Produced Sequences"), rows, empty_message="No outputs.", escape_cells=False),
    )


def _render_memory_stats(execution: ExecutionModel) -> str:
    rows = tuple(
        (
            memory_stat.resource_name,
            str(memory_stat.peak_allocated),
            str(memory_stat.current_allocated),
            str(memory_stat.total_allocated),
            str(memory_stat.total_deallocated),
        )
        for memory_stat in execution["memory_stats"]
    )
    return _detail_block(
        "Memory Statistics",
        _table(
            ("Resource", "Peak Allocated (B)", "Current Allocated (B)", "Total Allocated (B)", "Total Deallocated (B)"),
            rows,
            empty_message="No memory statistics.",
        ),
    )


def _output_sequence_numbers_html(output: OutputModel) -> str:
    sequences = output["produced_sequence_numbers"]
    return _sequence_numbers_html(
        sequences,
        first_new_index=len(sequences),
        message_ids_by_sequence=output["produced_sequence_message_ids"],
        channel_name=output["channel_name"],
    )


def _render_alignment(execution: ExecutionModel) -> str:
    alignment = execution["alignment"]
    if alignment is None:
        return _detail_block("Alignment", '        <p class="empty-state">No alignment result.</p>')
    aligned_inputs = alignment["aligned_inputs"]
    # fmt: off
    rows = tuple(
        (
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            str(aligned_input["input_name"]),
            _alignment_selection(aligned_input),
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            "yes" if bool(aligned_input["present"]) else "no",
            # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            "yes" if bool(aligned_input["is_reused"]) else "no",
        )
        for aligned_input in aligned_inputs
    )
    # fmt: on
    return _detail_block(
        f"Alignment: {alignment['aligner_name']}",
        _table(("Input", "Selection", "Present", "Reused"), rows, empty_message="No aligned inputs."),
    )


def _alignment_selection(aligned_input: AlignedInputModel) -> str:
    # pyrefly: ignore[unnecessary-type-conversion] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    if bool(aligned_input["is_batch"]):
        return f"{aligned_input['batch_begin_sequence_number']} to {aligned_input['batch_end_sequence_number']}"
    return str(aligned_input["selected_sequence_number"])


def _detail_block(title: str, body: str) -> str:
    return f"""        <div class="execution-detail-block">
          <h3>{_escape(title)}</h3>
{body}
        </div>"""


def _render_readiness(model: ReportModel) -> str:
    readiness = model.readiness
    missing_inputs = ", ".join(readiness.missing_inputs) if readiness.missing_inputs else "none"
    rows = tuple(
        (
            gap.id,
            gap.reason,
            gap.cog_instance_path,
            gap.channel_name,
            str(gap.execution_index),
            gap.message,
        )
        for gap in readiness.gaps
    )
    gap_table = _table(
        ("ID", "Reason", "Cog", "Channel", "Execution", "Message"),
        rows,
        empty_message="No readiness gaps.",
    )
    return f"""    <section {_report_section_attributes("readiness")}>
      <h2>Replay Readiness</h2>
      {
        _definition_list(
            (
                ("Status", readiness.status),
                ("State Snapshot Coverage", "yes" if readiness.has_state_snapshot else "no"),
                ("Missing Inputs", missing_inputs),
            )
        )
    }
{gap_table}
    </section>"""


def _render_state_snapshots(model: ReportModel) -> str:
    rows = tuple(
        (
            snapshot.id,
            snapshot.cog_instance_path,
            str(snapshot.snapshot_time_ns),
            snapshot.selection,
            snapshot.state_schema_uuid,
            str(snapshot.state_size_bytes),
        )
        for snapshot in model.state_snapshots
    )
    return f"""    <section {_report_section_attributes("state-snapshots")}>
      <h2>State Snapshots</h2>
{_table(("ID", "Cog", "Time", "Selection", "Schema UUID", "Bytes"), rows, empty_message="No state snapshots.")}
    </section>"""


def _render_anomalies(model: ReportModel) -> str:
    summary = ", ".join(f"{count.label}: {count.count}" for count in model.anomalies.summary_counts) or "none"
    rows = tuple(
        (
            item.id,
            item.severity,
            item.type,
            item.title,
            item.cog_instance_path,
            "" if item.execution_index is None else str(item.execution_index),
            item.channel_name,
            item.message,
        )
        for item in model.anomalies.items
    )
    return f"""    <section {_report_section_attributes("anomalies")}>
      <h2>Anomaly Highlights</h2>
      {_definition_list((("Summary", summary),))}
{_table(("ID", "Severity", "Type", "Title", "Cog", "Execution", "Channel", "Message"), rows, empty_message="No anomalies.")}
    </section>"""


def _definition_list(items: Sequence[tuple[str, str]]) -> str:
    entries = "\n".join(f"        <dt>{_escape(label)}</dt><dd>{_escape(value)}</dd>" for label, value in items)
    return f"""      <dl class="metadata-list">
{entries}
      </dl>"""


def _table(
    headers: Sequence[str],
    rows: Sequence[Sequence[str]],
    *,
    empty_message: str,
    escape_cells: bool = True,
) -> str:
    if not rows:
        return f'      <p class="empty-state">{_escape(empty_message)}</p>'
    header_html = "".join(f"<th>{_escape(header)}</th>" for header in headers)
    row_html = "\n".join(
        "        <tr>" + "".join(f"<td>{_escape(value) if escape_cells else value}</td>" for value in row) + "</tr>"
        for row in rows
    )
    return f"""      <div class="table-wrap">
        <table>
          <thead><tr>{header_html}</tr></thead>
          <tbody>
{row_html}
          </tbody>
        </table>
      </div>"""


def _escape(value: str) -> str:
    return html.escape(value)


def _timeline_label(label: str) -> str:
    path_parts = tuple(part for part in label.split(".") if part)
    if path_parts:
        label = path_parts[-1]
    else:
        module_parts = tuple(part for part in label.split("::") if part)
        if module_parts:
            label = module_parts[-1]
    if len(label) <= _TIMELINE_LABEL_MAX_LENGTH:
        return label
    return f"{label[: _TIMELINE_LABEL_MAX_LENGTH - 3]}..."


def _channel_flow_display_label(label: str) -> str:
    if label == "unknown source":
        return "external inputs"
    if label == "unknown sink":
        return "external outputs"
    return _short_cog_label(label)


def _channel_flow_label_html(label: str) -> str:
    display_label = _channel_flow_display_label(label)
    return f'<span title="{_escape(label)}">{_escape(display_label)}</span>'


def _execution_count_label(count: int) -> str:
    noun = "exec" if count == 1 else "execs"
    return f"{count:,} {noun}"


def _format_timeline_offset_ns(offset_ns: int) -> str:
    return f"+{offset_ns / _NANOSECONDS_PER_SECOND:.3f}s"


def _sequence_numbers_html(
    numbers: Sequence[int],
    *,
    first_new_index: int,
    message_ids_by_sequence: Mapping[int, str] | None = None,
    channel_name: str | None = None,
) -> str:
    if not numbers:
        return "none"
    parts = []
    for index, number in enumerate(numbers):
        escaped_number = _message_sequence_link_html(number, message_ids_by_sequence, channel_name)
        if index >= first_new_index:
            parts.append(f'<mark class="sequence-new">{escaped_number}</mark>')
        else:
            parts.append(escaped_number)
    return ", ".join(parts)


def _message_sequence_link_html(
    number: int,
    message_ids_by_sequence: Mapping[int, str] | None,
    channel_name: str | None,
) -> str:
    escaped_number = _escape(str(number))
    if message_ids_by_sequence is None or number not in message_ids_by_sequence:
        return escaped_number
    data_attributes = ""
    if channel_name is not None:
        data_attributes = (
            f' class="message-data-link" data-channel-name="{_escape(channel_name)}" '
            f'data-sequence-number="{number}" aria-expanded="false"'
        )
    return f'<a href="#{_escape(message_ids_by_sequence[number])}"{data_attributes}>{escaped_number}</a>'


def _timestamp_time(timestamp_ns: int) -> str:
    timestamp = _format_timestamp_ns(timestamp_ns)
    datetime_value = _timestamp_datetime(timestamp_ns)
    return f'<time class="timestamp" datetime="{datetime_value}" data-timestamp-ns="{timestamp_ns}">{timestamp}</time>'


def _timestamp_time_with_unix_ns(timestamp_ns: int) -> str:
    return f"{_timestamp_time(timestamp_ns)} ({timestamp_ns} ns)"


def _format_timestamp_ns(timestamp_ns: int) -> str:
    seconds, nanoseconds = divmod(timestamp_ns, _NANOSECONDS_PER_SECOND)
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(seconds))
    return f"{timestamp}.{nanoseconds:09d} UTC"


def _timestamp_datetime(timestamp_ns: int) -> str:
    seconds, nanoseconds = divmod(timestamp_ns, _NANOSECONDS_PER_SECOND)
    timestamp = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(seconds))
    return f"{timestamp}.{nanoseconds:09d}Z"


def _short_cog_label(label: str) -> str:
    path_parts = tuple(part for part in label.split(".") if part)
    if path_parts:
        return path_parts[-1]
    module_parts = tuple(part for part in label.split("::") if part)
    if module_parts:
        return module_parts[-1]
    return label


def _short_label(label: str, max_length: int) -> str:
    if len(label) <= max_length:
        return label
    return f"{label[: max_length - 3]}..."


def _read_asset(filename: str) -> str:
    """Read a checked-in renderer asset."""
    return (_ASSET_DIR / filename).read_text(encoding="utf-8")


def _read_package_file(filename: str) -> str:
    """Read a checked-in renderer package file."""
    return (_PACKAGE_DIR / filename).read_text(encoding="utf-8")
