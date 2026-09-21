/**
 * Copyright 2026 Stack AV Co.
 * SPDX-License-Identifier: Apache-2.0
 */

(() => {
  const SVG_NS = "http" + "://www.w3.org/2000/svg";
  const TIMELINE_MIN_WIDTH = 1120;
  const TIMELINE_LABEL_WIDTH = 300;
  const TIMELINE_RIGHT_PADDING = 18;
  const TIMELINE_TOP_PADDING = 34;
  const TIMELINE_BOTTOM_PADDING = 24;
  const TIMELINE_ROW_HEIGHT = 52;
  const TIMELINE_BAR_HEIGHT = 12;
  const TIMELINE_LABEL_MAX_LENGTH = 30;
  const TIMELINE_PIXELS_PER_EXECUTION = 4;
  const TIMELINE_TICK_COUNT = 5;
  const NANOSECONDS_PER_SECOND = 1000000000n;

  const dataElement = document.getElementById("journal-report-data");
  const rootElement = document.getElementById("journal-report-root");
  if (dataElement === null || rootElement === null) {
    return;
  }

  const model = JSON.parse(dataElement.textContent || "{}");
  renderTimeline(model.timeline);
  hydrateTimestampElements();
  initializeTimelineInteractions(model.timeline);
  initializeSectionTabs();
  initializeExecutionJump(model.timeline);
  initializeMessageDataLinks();
  rootElement.dataset.reportReady = "true";
  rootElement.dataset.reportTitle = model.title || "";

  function initializeSectionTabs() {
    const tabList = document.getElementById("report-section-tabs");
    const sections = Array.from(rootElement.querySelectorAll(".report-section")).filter((section) => section.id);
    if (tabList === null || sections.length <= 1) {
      if (tabList !== null) {
        tabList.hidden = true;
      }
      return;
    }

    tabList.setAttribute("role", "tablist");
    const tabs = new Map();
    const existingTabs = new Map(
      Array.from(tabList.querySelectorAll(".report-section-tab")).map((tab) => [
        tab.getAttribute("href")?.replace(/^#/, ""),
        tab,
      ]),
    );
    for (const section of sections) {
      const title = section.querySelector("h2")?.textContent?.trim() || section.id;
      let tab = existingTabs.get(section.id);
      if (tab === undefined) {
        tab = document.createElement("a");
        tab.className = "report-section-tab";
        tab.href = `#${section.id}`;
        tab.textContent = title;
        tabList.append(tab);
      }
      tab.id = `${section.id}-tab`;
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-controls", section.id);
      tab.addEventListener("click", (event) => {
        event.preventDefault();
        activateSection(section.id, { scrollToHashTarget: false });
        setHash(section.id);
      });
      tabs.set(section.id, tab);

      section.setAttribute("role", "tabpanel");
      section.setAttribute("aria-labelledby", tab.id);
    }

    const landingSection = sections.find((section) => section.id === "overview") || sections[0];
    activateSection(landingSection.id, { scrollToHashTarget: false });
    rootElement.dataset.sectionTabsReady = "true";
    window.addEventListener("hashchange", () => {
      const section = sectionForCurrentHash(sections);
      if (section !== null) {
        activateSection(section.id, { scrollToHashTarget: true });
      }
    });

    function activateSection(sectionId, options) {
      for (const section of sections) {
        const isActive = section.id === sectionId;
        section.classList.toggle("report-section-active", isActive);
        section.hidden = !isActive;
        const tab = tabs.get(section.id);
        if (tab !== undefined) {
          tab.classList.toggle("report-section-tab-active", isActive);
          tab.setAttribute("aria-selected", String(isActive));
          tab.tabIndex = isActive ? 0 : -1;
        }
      }

      if (options.scrollToHashTarget) {
        const target = currentHashTarget();
        if (target !== null && target.id !== sectionId) {
          window.setTimeout(() => target.scrollIntoView({ block: "start" }), 0);
        }
      }
    }
  }

  function setHash(id) {
    try {
      history.pushState(null, "", `#${id}`);
    } catch {
      window.location.hash = id;
    }
  }

  function sectionForCurrentHash(sections) {
    const target = currentHashTarget();
    if (target === null) {
      return null;
    }
    return sections.find((section) => section === target || section.contains(target)) || null;
  }

  function currentHashTarget() {
    if (window.location.hash.length <= 1) {
      return null;
    }
    return document.getElementById(decodeURIComponent(window.location.hash.slice(1)));
  }

  function hydrateTimestampElements() {
    for (const element of document.querySelectorAll("time[data-timestamp-ns]")) {
      const timestampNs = element.getAttribute("data-timestamp-ns");
      if (timestampNs !== null && /^[+-]?\d+$/.test(timestampNs)) {
        element.textContent = formatTimestampNs(timestampNs);
        element.setAttribute("datetime", formatTimestampDateTimeNs(timestampNs));
      }
    }
  }

  function initializeExecutionJump(timeline) {
    const controls = Array.from(document.querySelectorAll(".execution-jump-controls"));
    if (controls.length === 0) {
      return;
    }

    const bars = timelineBars(timeline);
    for (const control of controls) {
      initializeExecutionJumpControl(control, bars);
    }
  }

  function initializeMessageDataLinks() {
    const messageDataApi = rootElement.dataset.messageDataApi;
    if (messageDataApi === undefined) {
      return;
    }
    const panels = new WeakMap();
    for (const link of document.querySelectorAll(".message-data-link[data-channel-name][data-sequence-number]")) {
      link.addEventListener("click", (event) => {
        event.preventDefault();
        const existingPanel = panels.get(link);
        if (existingPanel !== undefined) {
          existingPanel.hidden = !existingPanel.hidden;
          link.setAttribute("aria-expanded", String(!existingPanel.hidden));
          return;
        }
        void loadMessageData(link, messageDataApi, panels);
      });
    }
  }

  async function loadMessageData(link, messageDataApi, panels) {
    const channelName = link.getAttribute("data-channel-name");
    const sequenceNumber = link.getAttribute("data-sequence-number");
    if (channelName === null || sequenceNumber === null) {
      return;
    }

    const panel = document.createElement("div");
    panel.className = "message-data-panel";
    const contents = document.createElement("pre");
    contents.textContent = "Loading message data...";
    panel.append(contents);
    panels.set(link, panel);
    const panelAnchor = link.closest(".sequence-new") || link;
    panelAnchor.after(panel);
    link.setAttribute("aria-expanded", "true");

    const query = new URLSearchParams({ channel: channelName, sequence: sequenceNumber });
    try {
      const response = await fetch(`${messageDataApi}?${query}`, { headers: { Accept: "application/json" } });
      if (!response.ok) {
        throw new Error(`Request failed (${response.status}).`);
      }
      contents.textContent = JSON.stringify(await response.json(), null, 2);
    } catch (error) {
      panel.classList.add("message-data-panel-error");
      contents.textContent = error instanceof Error ? error.message : "Message data could not be loaded.";
    }
  }

  function initializeExecutionJumpControl(control, bars) {
    const input = control.querySelector(".execution-jump-input");
    const button = control.querySelector(".execution-jump-button");
    const status = control.querySelector(".execution-jump-status");
    if (!(input instanceof HTMLInputElement) || !(button instanceof HTMLButtonElement) || status === null) {
      return;
    }

    const cogId = control.getAttribute("data-cog-id");
    const cogBars = cogId === null ? bars : bars.filter((bar) => bar.cog_id === cogId);
    if (cogBars.length === 0) {
      input.disabled = true;
      button.disabled = true;
      return;
    }

    const jump = () => {
      const timestampNs = parseTimestampNs(input.value);
      if (timestampNs === null) {
        status.textContent = "Invalid timestamp.";
        return;
      }
      const bar = nearestTimelineBar(cogBars, timestampNs);
      selectTimelineExecution(bar.execution_id);
      revealExecution(bar.execution_id);
      status.textContent = `Selected execution ${bar.execution_index} at ${formatTimestampNs(bar.start_time_ns)}.`;
    };

    button.addEventListener("click", jump);
    input.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        jump();
      }
    });
  }

  function renderTimeline(timeline) {
    const timelineRoot = document.getElementById("execution-timeline-root");
    if (timelineRoot === null || timeline === undefined || !hasTimelineBars(timeline)) {
      return;
    }
    if (timelineRoot.querySelector(".timeline-svg") !== null) {
      return;
    }

    const rowCount = timeline.rows.length;
    const height = TIMELINE_TOP_PADDING + rowCount * TIMELINE_ROW_HEIGHT + TIMELINE_BOTTOM_PADDING;
    const timelineWidth = timelineWidthFor(timeline);
    const plotWidth = timelineWidth - TIMELINE_LABEL_WIDTH - TIMELINE_RIGHT_PADDING;
    const timeRange = Math.max(1, timeline.end_time_ns - timeline.start_time_ns);
    const svg = createSvg("svg");
    svg.setAttribute("class", "timeline-svg");
    svg.setAttribute("width", String(timelineWidth));
    svg.setAttribute("height", String(height));
    svg.setAttribute("viewBox", `0 0 ${timelineWidth} ${height}`);
    svg.setAttribute("role", "img");
    svg.setAttribute("aria-label", "Execution timeline");

    appendTimelineAxis(svg, timeline, timelineWidth, plotWidth, height);

    for (const [rowIndex, row] of timeline.rows.entries()) {
      appendRow(svg, row, rowIndex, timelineWidth, plotWidth, timeline.start_time_ns, timeRange);
    }

    timelineRoot.replaceChildren(svg);
  }

  function timelineWidthFor(timeline) {
    const maxRowExecutionCount = timeline.rows.reduce((maxCount, row) => Math.max(maxCount, row.bars.length), 0);
    const minPlotWidth = TIMELINE_MIN_WIDTH - TIMELINE_LABEL_WIDTH - TIMELINE_RIGHT_PADDING;
    const plotWidth = Math.max(minPlotWidth, maxRowExecutionCount * TIMELINE_PIXELS_PER_EXECUTION);
    return TIMELINE_LABEL_WIDTH + TIMELINE_RIGHT_PADDING + plotWidth;
  }

  function appendTimelineAxis(svg, timeline, timelineWidth, plotWidth, height) {
    const axisY = TIMELINE_TOP_PADDING - 14;
    const axis = createSvg("line");
    axis.setAttribute("class", "timeline-axis");
    axis.setAttribute("x1", String(TIMELINE_LABEL_WIDTH));
    axis.setAttribute("x2", String(timelineWidth - TIMELINE_RIGHT_PADDING));
    axis.setAttribute("y1", String(axisY));
    axis.setAttribute("y2", String(axisY));
    svg.append(axis);

    const startTimeNs = Number(timeline.start_time_ns);
    const endTimeNs = Number(timeline.end_time_ns);
    const timeRange = Math.max(1, endTimeNs - startTimeNs);
    for (const tickTimeNs of timelineTicks(startTimeNs, endTimeNs)) {
      appendTimelineTick(svg, tickTimeNs, startTimeNs, timeRange, plotWidth, axisY, height);
    }
  }

  function appendTimelineTick(svg, tickTimeNs, startTimeNs, timeRange, plotWidth, axisY, height) {
    const x = TIMELINE_LABEL_WIDTH + ((tickTimeNs - startTimeNs) / timeRange) * plotWidth;
    const grid = createSvg("line");
    grid.setAttribute("class", "timeline-grid");
    grid.setAttribute("x1", x.toFixed(2));
    grid.setAttribute("x2", x.toFixed(2));
    grid.setAttribute("y1", String(axisY));
    grid.setAttribute("y2", String(height - TIMELINE_BOTTOM_PADDING + 4));
    svg.append(grid);

    const label = createSvg("text");
    label.setAttribute("class", "timeline-tick-label");
    label.setAttribute("x", x.toFixed(2));
    label.setAttribute("y", String(axisY - 7));
    label.textContent = formatTimelineOffsetNs(tickTimeNs - startTimeNs);
    svg.append(label);
  }

  function timelineTicks(startTimeNs, endTimeNs) {
    const timeRange = Math.max(1, endTimeNs - startTimeNs);
    return Array.from(
      { length: TIMELINE_TICK_COUNT },
      (_unused, index) => startTimeNs + Math.round((timeRange * index) / (TIMELINE_TICK_COUNT - 1)),
    );
  }

  function formatTimelineOffsetNs(offsetNs) {
    return `+${(offsetNs / Number(NANOSECONDS_PER_SECOND)).toFixed(3)}s`;
  }

  function appendRow(svg, row, rowIndex, timelineWidth, plotWidth, startTimeNs, timeRange) {
    const rowTopY = TIMELINE_TOP_PADDING + rowIndex * TIMELINE_ROW_HEIGHT;
    const rowY = rowTopY + TIMELINE_ROW_HEIGHT / 2;
    const background = createSvg("rect");
    background.setAttribute("class", rowIndex % 2 === 0 ? "timeline-row-bg timeline-row-bg-even" : "timeline-row-bg");
    background.setAttribute("x", "0");
    background.setAttribute("y", String(rowTopY));
    background.setAttribute("width", String(timelineWidth));
    background.setAttribute("height", String(TIMELINE_ROW_HEIGHT));
    svg.append(background);

    const label = createSvg("text");
    label.setAttribute("class", "timeline-label");
    label.setAttribute("x", "0");
    label.setAttribute("y", String(rowY));
    label.textContent = truncateLabel(shortCogLabel(row.cog_instance_path));
    const labelTitle = createSvg("title");
    labelTitle.textContent = row.cog_instance_path;
    label.append(labelTitle);
    svg.append(label);

    const count = createSvg("text");
    count.setAttribute("class", "timeline-row-count");
    count.setAttribute("x", String(TIMELINE_LABEL_WIDTH - 14));
    count.setAttribute("y", String(rowY));
    count.textContent = executionCountLabel(row.bars.length);
    svg.append(count);

    const lane = createSvg("line");
    lane.setAttribute("class", "timeline-lane");
    lane.setAttribute("x1", TIMELINE_LABEL_WIDTH);
    lane.setAttribute("x2", timelineWidth - TIMELINE_RIGHT_PADDING);
    lane.setAttribute("y1", String(rowY));
    lane.setAttribute("y2", String(rowY));
    svg.append(lane);

    for (const bar of row.bars) {
      appendBar(svg, bar, rowY, timelineWidth, plotWidth, startTimeNs, timeRange);
    }
  }

  function appendBar(svg, bar, rowY, timelineWidth, plotWidth, startTimeNs, timeRange) {
    const x = TIMELINE_LABEL_WIDTH + ((bar.start_time_ns - startTimeNs) / timeRange) * plotWidth;
    const width = Math.max(1, (bar.duration_ns / timeRange) * plotWidth);
    const hitX = Math.max(TIMELINE_LABEL_WIDTH, x - 4);
    const hitWidth = Math.max(10, Math.min(timelineWidth - TIMELINE_RIGHT_PADDING - hitX, width + 8));
    const barY = rowY - TIMELINE_BAR_HEIGHT / 2;
    const target = document.getElementById(bar.execution_id);
    const barTitle = timelineBarTitle(bar);
    const container = createSvg(target === null ? "g" : "a");
    if (target !== null) {
      container.setAttribute("href", `#${bar.execution_id}`);
    }
    container.setAttribute("aria-label", `Execution ${bar.execution_index}`);

    const title = createSvg("title");
    title.textContent = barTitle;
    container.append(title);

    const hitRect = createSvg("rect");
    hitRect.setAttribute("class", "timeline-bar-hit");
    hitRect.setAttribute("x", hitX.toFixed(2));
    hitRect.setAttribute("y", (barY - 6).toFixed(2));
    hitRect.setAttribute("width", hitWidth.toFixed(2));
    hitRect.setAttribute("height", String(TIMELINE_BAR_HEIGHT + 12));
    hitRect.setAttribute("rx", "4");
    setTimelineBarData(hitRect, bar);
    appendSvgTitle(hitRect, barTitle);
    container.append(hitRect);

    const rect = createSvg("rect");
    rect.setAttribute("class", `timeline-bar timeline-bar-${bar.duration_bucket}`);
    rect.setAttribute("x", x.toFixed(2));
    rect.setAttribute("y", barY.toFixed(2));
    rect.setAttribute("width", width.toFixed(2));
    rect.setAttribute("height", String(TIMELINE_BAR_HEIGHT));
    rect.setAttribute("rx", "2");
    setTimelineBarData(rect, bar);
    appendSvgTitle(rect, barTitle);
    container.append(rect);
    svg.append(container);
  }

  function appendSvgTitle(element, text) {
    const title = createSvg("title");
    title.textContent = text;
    element.append(title);
  }

  function setTimelineBarData(element, bar) {
    element.setAttribute("data-cog-id", bar.cog_id);
    element.setAttribute("data-execution-id", bar.execution_id);
    element.setAttribute("data-execution-index", String(bar.execution_index));
    element.setAttribute("data-start-time-ns", String(bar.start_time_ns));
    element.setAttribute("data-end-time-ns", String(bar.end_time_ns));
    element.setAttribute("data-duration-ns", String(bar.duration_ns));
  }

  function initializeTimelineInteractions(timeline) {
    const timelineRoot = document.getElementById("execution-timeline-root");
    if (timelineRoot === null) {
      return;
    }
    const barsByExecutionId = new Map(timelineBars(timeline).map((bar) => [bar.execution_id, bar]));
    if (barsByExecutionId.size === 0) {
      return;
    }

    const tooltip = document.createElement("div");
    tooltip.className = "timeline-tooltip";
    tooltip.hidden = true;
    rootElement.append(tooltip);

    for (const target of timelineRoot.querySelectorAll(".timeline-bar, .timeline-bar-hit")) {
      const executionId = target.getAttribute("data-execution-id");
      const bar = executionId === null ? undefined : barsByExecutionId.get(executionId);
      if (bar === undefined) {
        continue;
      }

      target.setAttribute("tabindex", "0");
      target.addEventListener("mouseenter", (event) => {
        showTimelineTooltip(tooltip, bar, event.clientX, event.clientY);
      });
      target.addEventListener("mousemove", (event) => {
        positionTimelineTooltip(tooltip, event.clientX, event.clientY);
      });
      target.addEventListener("mouseleave", () => {
        hideTimelineTooltip(tooltip);
      });
      target.addEventListener("focus", () => {
        const rect = target.getBoundingClientRect();
        showTimelineTooltip(tooltip, bar, rect.left + rect.width / 2, rect.top);
      });
      target.addEventListener("blur", () => {
        hideTimelineTooltip(tooltip);
      });
      target.addEventListener("click", (event) => {
        event.preventDefault();
        selectTimelineExecution(bar.execution_id);
        revealExecution(bar.execution_id);
      });
      target.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          selectTimelineExecution(bar.execution_id);
          revealExecution(bar.execution_id);
        }
      });
    }
  }

  function showTimelineTooltip(tooltip, bar, clientX, clientY) {
    tooltip.textContent = timelineBarTitle(bar);
    tooltip.hidden = false;
    positionTimelineTooltip(tooltip, clientX, clientY);
  }

  function hideTimelineTooltip(tooltip) {
    tooltip.hidden = true;
  }

  function positionTimelineTooltip(tooltip, clientX, clientY) {
    tooltip.style.left = `${clientX + 12}px`;
    tooltip.style.top = `${clientY + 12}px`;
  }

  function timelineBarTitle(bar) {
    return `Execution ${bar.execution_index}: start ${bar.start_time_ns} ns, end ${bar.end_time_ns} ns, duration ${bar.duration_ns} ns`;
  }

  function createSvg(tagName) {
    return document.createElementNS(SVG_NS, tagName);
  }

  function hasTimelineBars(timeline) {
    return timeline.rows.some((row) => row.bars.length > 0);
  }

  function timelineBars(timeline) {
    const domBars = timelineBarsFromDocument();
    if (domBars.length > 0) {
      return domBars;
    }
    if (timeline === undefined || !hasTimelineBars(timeline)) {
      return [];
    }
    return timeline.rows
      .flatMap((row) =>
        row.bars.map((bar) => ({
          ...bar,
          cog_id: row.cog_id,
          cog_instance_path: row.cog_instance_path,
          start_time_ns: toBigInt(bar.start_time_ns),
          end_time_ns: toBigInt(bar.end_time_ns),
          duration_ns: toBigInt(bar.duration_ns),
        })),
      )
      .sort((left, right) => compareBigInt(left.start_time_ns, right.start_time_ns));
  }

  function timelineBarsFromDocument() {
    const bars = [];
    for (const rect of document.querySelectorAll(".timeline-bar[data-execution-id][data-start-time-ns]")) {
      const executionId = rect.getAttribute("data-execution-id");
      const cogId = rect.getAttribute("data-cog-id");
      const executionIndex = rect.getAttribute("data-execution-index");
      const startTimeNs = rect.getAttribute("data-start-time-ns");
      const endTimeNs = rect.getAttribute("data-end-time-ns");
      const durationNs = rect.getAttribute("data-duration-ns");
      if (executionId === null || cogId === null || startTimeNs === null || endTimeNs === null || durationNs === null) {
        continue;
      }
      try {
        bars.push({
          cog_id: cogId,
          execution_id: executionId,
          execution_index: executionIndex === null ? "unknown" : executionIndex,
          start_time_ns: BigInt(startTimeNs),
          end_time_ns: BigInt(endTimeNs),
          duration_ns: BigInt(durationNs),
        });
      } catch {
        continue;
      }
    }
    return bars.sort((left, right) => compareBigInt(left.start_time_ns, right.start_time_ns));
  }

  function parseTimestampNs(rawValue) {
    const value = rawValue.trim();
    if (value.length === 0) {
      return null;
    }
    const numericMatch = value.match(/^([+-]?)(\d+)(?:\.(\d+))?$/);
    if (numericMatch !== null) {
      const sign = numericMatch[1] === "-" ? -1n : 1n;
      const whole = BigInt(numericMatch[2]);
      const fractional = numericMatch[3];
      if (fractional !== undefined) {
        const fractionalNs = BigInt(fractional.padEnd(9, "0").slice(0, 9));
        return sign * (whole * NANOSECONDS_PER_SECOND + fractionalNs);
      }
      return sign * whole;
    }

    const timestampMs = Date.parse(value);
    return Number.isFinite(timestampMs) ? BigInt(Math.trunc(timestampMs)) * 1000000n : null;
  }

  function nearestTimelineBar(bars, timestampNs) {
    let nearest = bars[0];
    let nearestDelta = timelineBarDistanceNs(nearest, timestampNs);
    for (const bar of bars.slice(1)) {
      const delta = timelineBarDistanceNs(bar, timestampNs);
      if (delta < nearestDelta) {
        nearest = bar;
        nearestDelta = delta;
      }
    }
    return nearest;
  }

  function timelineBarDistanceNs(bar, timestampNs) {
    if (timestampNs >= bar.start_time_ns && timestampNs <= bar.end_time_ns) {
      return 0n;
    }
    return timestampNs < bar.start_time_ns
      ? absBigInt(bar.start_time_ns - timestampNs)
      : absBigInt(timestampNs - bar.end_time_ns);
  }

  function selectTimelineExecution(executionId) {
    for (const rect of document.querySelectorAll(".timeline-bar")) {
      rect.classList.toggle("timeline-bar-selected", rect.getAttribute("data-execution-id") === executionId);
    }
  }

  function revealExecution(executionId) {
    const target = document.getElementById(executionId);
    if (target === null) {
      const selectedBar = document.querySelector(".timeline-bar-selected");
      selectedBar?.scrollIntoView({ block: "center", inline: "center" });
      return;
    }
    if (target instanceof HTMLDetailsElement) {
      target.open = true;
    }
    if (window.location.hash !== `#${executionId}`) {
      window.location.hash = executionId;
    } else {
      window.dispatchEvent(new Event("hashchange"));
    }
    window.setTimeout(() => target.scrollIntoView({ block: "start" }), 0);
  }

  function formatTimestampNs(timestampNs) {
    return `${formatTimestampParts(timestampNs).dateTime.replace("T", " ")} UTC`;
  }

  function formatTimestampDateTimeNs(timestampNs) {
    return `${formatTimestampParts(timestampNs).dateTime}Z`;
  }

  function formatTimestampParts(timestampNs) {
    let rawNanoseconds = toBigInt(timestampNs);
    let seconds = rawNanoseconds / NANOSECONDS_PER_SECOND;
    let nanoseconds = rawNanoseconds % NANOSECONDS_PER_SECOND;
    if (nanoseconds < 0n) {
      seconds -= 1n;
      nanoseconds += NANOSECONDS_PER_SECOND;
    }
    const timestamp = new Date(Number(seconds) * 1000);
    const baseTime = timestamp.toISOString().slice(0, 19);
    return {
      dateTime: `${baseTime}.${nanoseconds.toString().padStart(9, "0")}`,
    };
  }

  function toBigInt(value) {
    if (typeof value === "bigint") {
      return value;
    }
    if (typeof value === "string") {
      return BigInt(value);
    }
    return BigInt(Math.trunc(Number(value)));
  }

  function absBigInt(value) {
    return value < 0n ? -value : value;
  }

  function compareBigInt(left, right) {
    if (left < right) {
      return -1;
    }
    if (left > right) {
      return 1;
    }
    return 0;
  }

  function truncateLabel(label) {
    if (label.length <= TIMELINE_LABEL_MAX_LENGTH) {
      return label;
    }
    return `${label.slice(0, TIMELINE_LABEL_MAX_LENGTH - 3)}...`;
  }

  function executionCountLabel(count) {
    return `${count.toLocaleString("en-US")} ${count === 1 ? "exec" : "execs"}`;
  }

  function shortCogLabel(label) {
    const pathParts = label.split(".").filter((part) => part.length > 0);
    if (pathParts.length > 0) {
      return pathParts[pathParts.length - 1];
    }
    const moduleParts = label.split("::").filter((part) => part.length > 0);
    if (moduleParts.length > 0) {
      return moduleParts[moduleParts.length - 1];
    }
    return label;
  }
})();
