# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Signal extraction API for analyzing observability data from log files.

This module provides a Python API for accessing signal metadata and extracting
report group data from Clockwork log files. It supports:

- Reading signal description metadata from log files
- Querying available cog classes, instances, report groups, and signals
- Streaming extraction of report group data with lazy per-channel reading
- Bulk extraction of report group data with flexible filtering
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from types import SimpleNamespace
from typing import TYPE_CHECKING, Any, Final

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator

from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.tools.signal_extractor.report_group_decoder import (
    ReportGroupDecodeContext,
    decode_aggregation_fields,
    decode_batch_entries,
    make_report_group_decode_context,
)

# Channel name constants for discovering signal metadata in logs.
_SIGNAL_METADATA_CHANNEL_PREFIX: Final[str] = "/clockwork/signal_metadata"
_SIGNAL_METADATA_CHANNEL_SUFFIXES: Final[list[str]] = ["", "/c1", "/c2", "/l1", "/l2", "/r1", "/r2"]


# --- Filter ---


@dataclass
class SignalExtractionFilter:
    """Filter specification for signal extraction.

    All filter fields are optional and default to None (no filtering).
    When multiple fields are specified, they are combined with AND logic.
    Each list field uses OR logic within itself.
    """

    cog_paths: list[str] | None = None
    cog_instance_paths: list[str] | None = None
    report_group_names: list[str] | None = None
    signal_ids: list[str] | None = None
    signal_instance_ids: list[str] | None = None
    start_time_ns: int | None = None
    end_time_ns: int | None = None


# --- Extracted Data Structures ---


@dataclass
class ReportGroupSignalInfo:
    """Metadata about a signal within a report group."""

    signal_name: str
    signal_index: int
    alias: str | None
    pre_aggregation_types: list[str]
    post_aggregation_types: list[str]


@dataclass
class ReportGroupAggregation:
    """Data from a single post-aggregated report group message.

    The ``fields`` dictionary maps schema field names to their values.
    Field names follow the ``{signal}_{pre_agg}_{post_agg}`` naming convention
    defined in PDD-933.
    """

    publish_time_ns: int
    execution_count: int
    execution_interval_ns: int
    fields: dict[str, object | None]


@dataclass
class ReportGroupBatch:
    """Data from a single batched report group message.

    Each entry in ``entries`` corresponds to one cog execution within the batch.
    Keys in each entry dictionary are the schema field names following the
    ``{signal}_{pre_agg}`` naming convention, plus preserved ``*_metadata``
    fields when a signal has metadata.
    """

    publish_time_ns: int
    execution_count: int
    execution_interval_ns: int
    entries: list[dict[str, object | None]]


@dataclass
class ExtractedReportGroup:
    """A fully extracted report group with context and data."""

    report_group_name: str
    cog_path: str
    cog_instance_path: str
    channel_name: str
    log_type: str
    report_group_type: str
    aggregation_size: int
    min_duration_ns: int
    max_duration_ns: int
    signal_info: list[ReportGroupSignalInfo]
    data: list[ReportGroupAggregation | ReportGroupBatch]


@dataclass
class BulkSignalData:
    """Container for bulk extracted signal data with indexes for quick lookup."""

    report_groups: list[ExtractedReportGroup]
    by_cog_path: dict[str, list[ExtractedReportGroup]]
    by_cog_instance_path: dict[str, list[ExtractedReportGroup]]


# --- Metadata Merging ---


def merge_metadata(  # noqa: C901 (Complexity is from nested loops, reads better as one method)
    metadata_list: list[Any],
) -> SimpleNamespace:  # elements are dynamically generated tachyon dataclasses with no static definition
    """Merge multiple SignalMetadataConfig objects into a single unified metadata view.

    Each SignalMetadataConfig uses array indexes that are local to its own arrays.
    This function remaps all such indexes to produce a globally consistent merged
    configuration with the following semantics:

    - ``signal_instance_names`` are deduplicated by name.
    - ``signals`` are deduplicated by name; ``signal_instance_indexes`` from all
      channels are unioned using the merged global instance-name indexes.
    - ``cogs`` are deduplicated by ``cog_path``; the first occurrence wins and
      its signal indexes are remapped to the merged ``signals`` array.
    - ``cog_instances`` are concatenated (unique across channels by contract).
    - ``report_group_channels`` are concatenated.

    Args:
        metadata_list: List of SignalMetadataConfig objects, one per channel.

    Returns:
        A ``SimpleNamespace`` with the same attribute interface as
        ``SignalMetadataConfig``.
    """
    if len(metadata_list) == 1:
        # pyrefly: ignore[no-any-return-explicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return metadata_list[0]

    merged_signal_instance_names: list[str] = []
    instance_name_to_merged_idx: dict[str, int] = {}

    merged_signals: list[SimpleNamespace] = []
    signal_name_to_merged_idx: dict[str, int] = {}

    merged_cogs: list[SimpleNamespace] = []
    cog_class_to_merged_idx: dict[str, int] = {}

    merged_cog_instances: list[Any] = []
    merged_report_group_channels: list[Any] = []

    for channel_meta in metadata_list:
        # Step 1: Remap signal instance names, deduplicating by name.
        instance_name_remap: dict[int, int] = {}
        for old_idx, name in enumerate(channel_meta.signal_instance_names):
            name_str = str(name)
            if name_str not in instance_name_to_merged_idx:
                instance_name_to_merged_idx[name_str] = len(merged_signal_instance_names)
                merged_signal_instance_names.append(name_str)
            instance_name_remap[old_idx] = instance_name_to_merged_idx[name_str]

        # Step 2: Remap signal indexes, deduplicating signals by name.
        signal_index_remap: dict[int, int] = {}
        for old_idx, sig in enumerate(channel_meta.signals):
            sig_name = str(sig.name)
            if sig_name not in signal_name_to_merged_idx:
                new_idx = len(merged_signals)
                signal_name_to_merged_idx[sig_name] = new_idx
                remapped_instance_indexes = [
                    instance_name_remap[i] for i in sig.signal_instance_indexes if i in instance_name_remap
                ]
                merged_signals.append(
                    SimpleNamespace(
                        name=sig_name,
                        pre_aggregation_type=sig.pre_aggregation_type,
                        pre_aggregation_definition=getattr(sig, "pre_aggregation_definition", ""),
                        signal_instance_indexes=remapped_instance_indexes,
                    )
                )
            else:
                # Signal already exists; union in any new instance indexes.
                new_idx = signal_name_to_merged_idx[sig_name]
                existing = merged_signals[new_idx]
                existing_set = set(existing.signal_instance_indexes)
                for old_inst_idx in sig.signal_instance_indexes:
                    new_inst_idx = instance_name_remap.get(old_inst_idx)
                    if new_inst_idx is not None and new_inst_idx not in existing_set:
                        existing.signal_instance_indexes.append(new_inst_idx)
                        existing_set.add(new_inst_idx)
            signal_index_remap[old_idx] = signal_name_to_merged_idx[sig_name]

        # Step 3: Merge cog classes, deduplicating by cog_path.
        # Signal indexes within report group signals are remapped to the merged signals array.
        for cog in channel_meta.cogs:
            class_id = str(cog.cog_path)
            if class_id not in cog_class_to_merged_idx:
                cog_class_to_merged_idx[class_id] = len(merged_cogs)
                remapped_rgs = [
                    SimpleNamespace(
                        name=rg.name,
                        log_type=rg.log_type,
                        report_group_type=rg.report_group_type,
                        aggregation_size=rg.aggregation_size,
                        min_duration=rg.min_duration,
                        max_duration=rg.max_duration,
                        signals=[
                            SimpleNamespace(
                                signal_index=signal_index_remap.get(rg_sig.signal_index, rg_sig.signal_index),
                                post_aggregation_types=rg_sig.post_aggregation_types,
                                alias=rg_sig.alias,
                                validity_source=getattr(rg_sig, "validity_source", 0),
                                validity_index=getattr(rg_sig, "validity_index", 0),
                            )
                            for rg_sig in rg.signals
                        ],
                    )
                    for rg in cog.report_groups
                ]
                merged_cogs.append(
                    SimpleNamespace(
                        cog_path=cog.cog_path,
                        report_groups=remapped_rgs,
                    )
                )

        # Step 4: Accumulate cog instances (unique across channels by contract).
        merged_cog_instances.extend(channel_meta.cog_instances)

        # Step 5: Accumulate report group channels.
        merged_report_group_channels.extend(channel_meta.report_group_channels)

    return SimpleNamespace(
        signal_instance_names=merged_signal_instance_names,
        signals=merged_signals,
        cogs=merged_cogs,
        cog_instances=merged_cog_instances,
        report_group_channels=merged_report_group_channels,
    )


# --- Streaming Data Structure ---


class StreamingReportGroup:
    """A streaming view of a report group with lazy data access.

    Provides metadata about the report group and a ``batches()`` method
    for lazily iterating over the actual report group messages.  Data is
    only read from the log file when ``batches()`` is called.
    """

    def __init__(  # noqa: PLR0913 # StreamingReportGroup bundles all report group metadata fields as required keyword-only arguments
        self,
        *,
        report_group_name: str,
        cog_path: str,
        cog_instance_path: str,
        channel_name: str,
        log_type: str,
        report_group_type: str,
        aggregation_size: int,
        min_duration_ns: int,
        max_duration_ns: int,
        signal_info: list[ReportGroupSignalInfo],
        batch_factory: Callable[[], Iterator[ReportGroupBatch | ReportGroupAggregation]],
    ) -> None:
        """Initialize a StreamingReportGroup with metadata and a lazy batch factory."""
        self.report_group_name = report_group_name
        self.cog_path = cog_path
        self.cog_instance_path = cog_instance_path
        self.channel_name = channel_name
        self.log_type = log_type
        self.report_group_type = report_group_type
        self.aggregation_size = aggregation_size
        self.min_duration_ns = min_duration_ns
        self.max_duration_ns = max_duration_ns
        self.signal_info = signal_info
        self._batch_factory = batch_factory

    def batches(self) -> Iterator[ReportGroupBatch | ReportGroupAggregation]:
        """Lazily iterate over batches in this report group.

        Each call creates a new iterator that reads from the log file.
        Messages are extracted and yielded on demand.

        Yields:
            ``ReportGroupBatch`` or ``ReportGroupAggregation`` for each message.
        """
        yield from self._batch_factory()


# --- SignalExtractor ---


class SignalExtractor:
    """Main class for extracting observability signal data from log files.

    Reads signal description metadata and report group data from Clockwork
    log files. Supports querying available signals and bulk extraction.
    """

    def __init__(self, log_uri: str) -> None:
        """Initialize the extractor with a log file URI.

        Args:
            log_uri: Path or URI to the Clockwork log file.
        """
        self._log_uri = log_uri
        self._metadata: Any | None = None
        self._cog_class_index: dict[str, Any] | None = None
        self._cog_instance_index: dict[str, Any] | None = None
        self._instances_by_class: dict[str, list[Any]] | None = None

    def _ensure_metadata_loaded(self) -> None:
        """Lazily load signal metadata from the log file on first access."""
        if self._metadata is not None:
            return
        self._metadata = self._read_signal_metadata()
        self._build_indexes()

    def _read_signal_metadata(self) -> Any:  # noqa: ANN401 # return type is a dynamically generated tachyon dataclass or SimpleNamespace with no static definition
        """Read signal metadata from the log file.

        Searches for signal metadata channels across all compute-unit suffixes
        and reads the first persistent SignalMetadataConfig message from each
        channel found.  When multiple channels exist their metadata is merged
        into a single unified view with globally consistent indexes.

        Returns:
            The deserialized SignalMetadataConfig tachyon object, or a
            ``SimpleNamespace`` with the same attribute interface when metadata
            from multiple channels has been merged.

        Raises:
            ValueError: If no signal metadata is found in the log.
        """
        reader = LogReader(self._log_uri)
        signal_metadata_channels = [
            _SIGNAL_METADATA_CHANNEL_PREFIX + suffix for suffix in _SIGNAL_METADATA_CHANNEL_SUFFIXES
        ]

        for channel in signal_metadata_channels:
            reader.add_topic(channel)

        per_channel_metadata = [message.message for message in reader.messages()]
        if not per_channel_metadata:
            msg = f"No signal metadata messsages found in log: {self._log_uri}"
            raise ValueError(msg)

        return merge_metadata(per_channel_metadata)

    def _build_indexes(self) -> None:
        """Build lookup indexes from the loaded metadata."""
        assert self._metadata is not None
        metadata = self._metadata
        self._cog_class_index = {}
        for cog in metadata.cogs:
            self._cog_class_index[str(cog.cog_path)] = cog

        self._cog_instance_index = {}
        self._instances_by_class = defaultdict(list)
        for instance in metadata.cog_instances:
            instance_id = str(instance.cog_instance_path)
            class_id = str(instance.cog_path)
            self._cog_instance_index[instance_id] = instance
            self._instances_by_class[class_id].append(instance)

    def get_metadata(self) -> Any:  # noqa: ANN401 # return type is a dynamically generated tachyon dataclass with no static definition
        """Get the raw signal description metadata from the log.

        Returns:
            The deserialized SignalMetadataConfig tachyon object.
        """
        self._ensure_metadata_loaded()
        return self._metadata

    def list_cog_paths(self) -> list[str]:
        """List all cog class paths present in the signal metadata.

        Returns:
            List of fully-qualified cog class path strings.
        """
        self._ensure_metadata_loaded()
        assert self._cog_class_index is not None
        return list(self._cog_class_index.keys())

    def list_cog_instance_paths(self, cog_path: str | None = None) -> list[str]:
        """List cog instance paths, optionally filtered by cog class path.

        Args:
            cog_path: Optional cog class path string to filter by.

        Returns:
            List of fully-qualified cog instance path strings.
        """
        self._ensure_metadata_loaded()
        assert self._cog_instance_index is not None
        assert self._instances_by_class is not None

        if cog_path is not None:
            return [str(inst.cog_instance_path) for inst in self._instances_by_class.get(cog_path, [])]
        return list(self._cog_instance_index.keys())

    def list_report_groups(self, cog_instance_path: str | None = None) -> list[str]:
        """List report group names, optionally filtered by cog instance.

        Args:
            cog_instance_path: Optional cog instance path string to filter by.

        Returns:
            List of report group names.
        """
        self._ensure_metadata_loaded()
        assert self._cog_instance_index is not None
        assert self._cog_class_index is not None

        if cog_instance_path is not None:
            instance = self._cog_instance_index.get(cog_instance_path)
            if instance is None:
                return []
            cog_class = self._cog_class_index.get(str(instance.cog_path))
            if cog_class is None:
                return []
            instance_rg_indexes = {rg_inst.report_group_index for rg_inst in instance.report_group_instances}
            return [rg.name for i, rg in enumerate(cog_class.report_groups) if i in instance_rg_indexes]

        seen: set[str] = set()
        result: list[str] = []
        assert self._metadata is not None
        for cog in self._metadata.cogs:
            for rg in cog.report_groups:
                if rg.name not in seen:
                    seen.add(rg.name)
                    result.append(rg.name)
        return result

    def list_signals(self, report_group_name: str | None = None) -> list[str]:
        """List signal names, optionally filtered by report group.

        Args:
            report_group_name: Optional report group name to filter by.

        Returns:
            List of signal names.
        """
        self._ensure_metadata_loaded()
        assert self._metadata is not None
        metadata = self._metadata

        if report_group_name is not None:
            signal_indexes: set[int] = set()
            for cog in metadata.cogs:
                for rg in cog.report_groups:
                    if rg.name == report_group_name:
                        for sig in rg.signals:
                            signal_indexes.add(sig.signal_index)
            return [metadata.signals[idx].name for idx in sorted(signal_indexes) if idx < len(metadata.signals)]

        return [sig.name for sig in metadata.signals]

    def list_signal_instance_names(self) -> list[str]:
        """List all signal instance names in the system.

        Returns:
            List of signal instance name strings.
        """
        self._ensure_metadata_loaded()
        assert self._metadata is not None
        return list(self._metadata.signal_instance_names)

    # --- Streaming ---

    def stream(
        self,
        extraction_filter: SignalExtractionFilter | None = None,
    ) -> Iterator[StreamingReportGroup]:
        """Stream report groups matching the filter.

        Returns an iterator of ``StreamingReportGroup`` instances whose
        ``batches()`` method lazily reads data from the log file.

        Args:
            extraction_filter: Optional filter to restrict which report groups
                are returned.  When ``None``, all report groups are streamed.

        Yields:
            ``StreamingReportGroup`` instances with metadata and lazy batch access.
        """
        self._ensure_metadata_loaded()
        assert self._metadata is not None

        if extraction_filter is None:
            extraction_filter = SignalExtractionFilter()

        channels = self._resolve_channels(extraction_filter)
        signal_index_filter = self._resolve_signal_index_filter(extraction_filter)
        field_filter_cache: dict[str, set[str] | None] = {}

        for channel_name, channel_info in channels.items():
            rg_metadata = channel_info["report_group"]
            cog_path = channel_info["cog_path"]
            cog_instance_path = channel_info["cog_instance_path"]
            rg_type_str = str(rg_metadata.report_group_type)

            signal_info = self._build_signal_info(rg_metadata)
            if signal_index_filter is not None:
                signal_info = [si for si in signal_info if si.signal_index in signal_index_filter]

            rg_name = rg_metadata.name
            if rg_name not in field_filter_cache:
                field_filter_cache[rg_name] = self._build_signal_field_filter(
                    rg_metadata,
                    extraction_filter,
                )
            field_filter = field_filter_cache[rg_name]
            decode_context = make_report_group_decode_context(self._metadata, rg_metadata)

            # Closure factory captures loop variables by value.
            def _make_batch_factory(
                ch: str,
                rg_type: str,
                ef: SignalExtractionFilter,
                ff: set[str] | None,
                dc: ReportGroupDecodeContext,
            ) -> Callable[[], Iterator[ReportGroupBatch | ReportGroupAggregation]]:
                def _factory() -> Iterator[ReportGroupBatch | ReportGroupAggregation]:
                    return self._stream_channel_messages(ch, rg_type, ef, ff, dc)

                return _factory

            yield StreamingReportGroup(
                report_group_name=rg_name,
                cog_path=cog_path,
                cog_instance_path=cog_instance_path,
                channel_name=channel_name,
                log_type=str(rg_metadata.log_type),
                report_group_type=rg_type_str,
                aggregation_size=rg_metadata.aggregation_size,
                min_duration_ns=rg_metadata.min_duration,
                max_duration_ns=rg_metadata.max_duration,
                signal_info=signal_info,
                batch_factory=_make_batch_factory(
                    channel_name,
                    rg_type_str,
                    extraction_filter,
                    field_filter,
                    decode_context,
                ),
            )

    def _stream_channel_messages(
        self,
        channel_name: str,
        report_group_type: str,
        extraction_filter: SignalExtractionFilter,
        signal_field_filter: set[str] | None,
        decode_context: ReportGroupDecodeContext,
    ) -> Iterator[ReportGroupBatch | ReportGroupAggregation]:
        """Lazily read and yield messages from a single channel.

        Creates a dedicated ``LogReader`` for the channel and yields extracted
        messages one at a time, applying time and signal-field filters.

        Args:
            channel_name: The log channel to read from.
            report_group_type: String indicating batched vs aggregated type.
            extraction_filter: Filter with time-range constraints.
            signal_field_filter: Set of field-name prefixes to keep, or ``None``.
            decode_context: Metadata required to decode signal validity.

        Yields:
            ``ReportGroupBatch`` or ``ReportGroupAggregation`` per matching message.
        """
        log_reader = LogReader(self._log_uri)
        log_reader.add_topic(channel_name)

        is_batched = "Batched" in report_group_type

        for message in log_reader.messages():
            publish_time_ns = message.publish_time.nanoseconds
            if extraction_filter.start_time_ns is not None and publish_time_ns < extraction_filter.start_time_ns:
                continue
            if extraction_filter.end_time_ns is not None and publish_time_ns > extraction_filter.end_time_ns:
                continue

            if is_batched:
                result: ReportGroupBatch | ReportGroupAggregation = _extract_batch_message(
                    publish_time_ns,
                    message.message,
                    decode_context,
                )
                if signal_field_filter is not None:
                    result = _filter_batch_fields(result, signal_field_filter)  # type: ignore[arg-type]
            else:
                result = _extract_aggregation_message(publish_time_ns, message.message, decode_context)
                if signal_field_filter is not None:
                    result = _filter_aggregation_fields(result, signal_field_filter)
            yield result

    def _build_signal_field_filter(
        self,
        rg_metadata: Any,  # noqa: ANN401 # parameter type is a dynamically generated tachyon dataclass with no static definition
        extraction_filter: SignalExtractionFilter,
    ) -> set[str] | None:
        """Build a set of field-name prefixes to keep based on signal filters.

        Returns ``None`` when no signal filtering is active, meaning all
        fields should be kept.
        """
        if extraction_filter.signal_ids is None and extraction_filter.signal_instance_ids is None:
            return None

        assert self._metadata is not None
        signal_index_filter = self._resolve_signal_index_filter(extraction_filter)
        if signal_index_filter is None:
            return None

        allowed_prefixes: set[str] = set()
        for rg_sig in rg_metadata.signals:
            if rg_sig.signal_index in signal_index_filter:
                sig_meta = self._metadata.signals[rg_sig.signal_index]
                prefix = rg_sig.alias or sig_meta.name
                allowed_prefixes.add(prefix + "_")

        return allowed_prefixes or set()

    # --- Bulk Extraction ---

    def bulk_extract(
        self,
        extraction_filter: SignalExtractionFilter | None = None,
    ) -> BulkSignalData:
        """Extract signal data in bulk according to the filter.

        Internally uses ``stream()`` to iterate over matching report groups
        and collects all data into a ``BulkSignalData`` object.

        Args:
            extraction_filter: Optional filter to restrict which data is extracted.
                When ``None``, all report group data is extracted.

        Returns:
            ``BulkSignalData`` containing the extracted report groups and indexes.
        """
        report_groups: list[ExtractedReportGroup] = []

        for streaming_rg in self.stream(extraction_filter):
            data = list(streaming_rg.batches())
            if not data:
                continue
            report_groups.append(
                ExtractedReportGroup(
                    report_group_name=streaming_rg.report_group_name,
                    cog_path=streaming_rg.cog_path,
                    cog_instance_path=streaming_rg.cog_instance_path,
                    channel_name=streaming_rg.channel_name,
                    log_type=streaming_rg.log_type,
                    report_group_type=streaming_rg.report_group_type,
                    aggregation_size=streaming_rg.aggregation_size,
                    min_duration_ns=streaming_rg.min_duration_ns,
                    max_duration_ns=streaming_rg.max_duration_ns,
                    signal_info=streaming_rg.signal_info,
                    data=data,
                )
            )

        by_cog_path: dict[str, list[ExtractedReportGroup]] = defaultdict(list)
        by_cog_instance_path: dict[str, list[ExtractedReportGroup]] = defaultdict(list)
        for rg in report_groups:
            by_cog_path[rg.cog_path].append(rg)
            by_cog_instance_path[rg.cog_instance_path].append(rg)

        return BulkSignalData(
            report_groups=report_groups,
            by_cog_path=dict(by_cog_path),
            by_cog_instance_path=dict(by_cog_instance_path),
        )

    def _resolve_signal_index_filter(
        self,
        extraction_filter: SignalExtractionFilter,
    ) -> set[int] | None:
        """Build a set of signal indexes matching signal_ids and signal_instance_ids.

        When both ``signal_ids`` and ``signal_instance_ids`` are specified the
        result is the intersection (AND) of the two sets.

        Returns:
            A set of signal indexes to include, or ``None`` if no signal filter
            is active.
        """
        assert self._metadata is not None
        if extraction_filter.signal_ids is None and extraction_filter.signal_instance_ids is None:
            return None

        result: set[int] | None = None

        if extraction_filter.signal_ids is not None:
            signal_id_set = set(extraction_filter.signal_ids)
            result = {idx for idx, sig in enumerate(self._metadata.signals) if sig.name in signal_id_set}

        if extraction_filter.signal_instance_ids is not None:
            instance_name_to_idx: dict[str, int] = {
                name: idx for idx, name in enumerate(self._metadata.signal_instance_names)
            }
            target_instance_idxs = {
                instance_name_to_idx[name]
                for name in extraction_filter.signal_instance_ids
                if name in instance_name_to_idx
            }
            matching: set[int] = set()
            for idx, sig in enumerate(self._metadata.signals):
                if set(sig.signal_instance_indexes) & target_instance_idxs:
                    matching.add(idx)
            result = (result & matching) if result is not None else matching

        return result

    def _resolve_channels(
        self,
        extraction_filter: SignalExtractionFilter,
    ) -> dict[str, dict[str, Any]]:
        """Determine which log channels to read based on the filter and metadata.

        Cross-references the filter with cog instances and report group channel
        metadata to produce a mapping from channel name to context info.

        Returns:
            A dict mapping channel_name to a dict with keys:
            ``report_group``, ``cog_path``, ``cog_instance_path``.
        """
        assert self._metadata is not None
        assert self._cog_class_index is not None
        assert self._cog_instance_index is not None

        signal_index_filter = self._resolve_signal_index_filter(extraction_filter)
        channels: dict[str, dict[str, Any]] = {}

        for rg_channel in self._metadata.report_group_channels:
            cog_path = str(rg_channel.cog_path)
            cog_instance_path = str(rg_channel.cog_instance_path)

            # Apply cog class / instance filters.
            if extraction_filter.cog_paths is not None and cog_path not in extraction_filter.cog_paths:
                continue
            if (
                extraction_filter.cog_instance_paths is not None
                and cog_instance_path not in extraction_filter.cog_instance_paths
            ):
                continue

            # Look up report group metadata from the cog class.
            rg_metadata = self._lookup_report_group(cog_path, rg_channel.report_group_index)
            if rg_metadata is None:
                continue

            # Apply report group name filter.
            if (
                extraction_filter.report_group_names is not None
                and rg_metadata.name not in extraction_filter.report_group_names
            ):
                continue

            # Apply signal filter: skip report group if it doesn't contain any requested signal.
            if signal_index_filter is not None:
                rg_signal_indexes = {sig.signal_index for sig in rg_metadata.signals}
                if not rg_signal_indexes & signal_index_filter:
                    continue

            channels[rg_channel.channel_name] = {
                "report_group": rg_metadata,
                "cog_path": cog_path,
                "cog_instance_path": cog_instance_path,
            }

        return channels

    def _lookup_report_group(self, cog_path: str, rg_index: int) -> Any | None:  # noqa: ANN401 # return type is a dynamically generated tachyon dataclass with no static definition
        """Look up a report group by cog class path and index.

        Returns:
            The report group metadata, or ``None`` if not found.
        """
        assert self._cog_class_index is not None
        cog_class_meta = self._cog_class_index.get(cog_path)
        if cog_class_meta is None or rg_index >= len(cog_class_meta.report_groups):
            return None
        # pyrefly: ignore[no-any-return-implicit] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
        return cog_class_meta.report_groups[rg_index]

    def _build_signal_info(
        self,
        rg_metadata: Any,  # noqa: ANN401 # parameter type is a dynamically generated tachyon dataclass with no static definition
    ) -> list[ReportGroupSignalInfo]:
        """Build ReportGroupSignalInfo list from report group metadata."""
        assert self._metadata is not None
        result: list[ReportGroupSignalInfo] = []
        for rg_sig in rg_metadata.signals:
            sig_index = rg_sig.signal_index
            sig_meta = self._metadata.signals[sig_index] if sig_index < len(self._metadata.signals) else None
            sig_name = sig_meta.name if sig_meta else f"signal_{sig_index}"
            pre_aggs = [str(a) for a in sig_meta.pre_aggregation_type] if sig_meta else []
            post_aggs = [str(a) for a in rg_sig.post_aggregation_types]
            result.append(
                ReportGroupSignalInfo(
                    signal_name=sig_name,
                    signal_index=sig_index,
                    alias=rg_sig.alias,
                    pre_aggregation_types=pre_aggs,
                    post_aggregation_types=post_aggs,
                )
            )
        return result


def _extract_aggregation_message(
    publish_time_ns: int,
    tachyon_msg: Any,  # noqa: ANN401 # parameter type is a dynamically generated tachyon dataclass with no static definition
    decode_context: ReportGroupDecodeContext | None = None,
) -> ReportGroupAggregation:
    """Extract a single post-aggregated report group message into a ReportGroupAggregation."""
    execution_count = getattr(tachyon_msg, "execution_count", 0)
    execution_interval = getattr(tachyon_msg, "execution_interval", 0)

    fields = decode_aggregation_fields(tachyon_msg, decode_context)

    return ReportGroupAggregation(
        publish_time_ns=publish_time_ns,
        execution_count=execution_count,
        execution_interval_ns=execution_interval,
        fields=fields,
    )


def _extract_batch_message(
    publish_time_ns: int,
    tachyon_msg: Any,  # noqa: ANN401 # parameter type is a dynamically generated tachyon dataclass with no static definition
    decode_context: ReportGroupDecodeContext | None = None,
) -> ReportGroupBatch:
    """Extract a single batched report group message into a ReportGroupBatch."""
    execution_interval = getattr(tachyon_msg, "execution_interval", 0)
    entries = decode_batch_entries(tachyon_msg, decode_context)

    return ReportGroupBatch(
        publish_time_ns=publish_time_ns,
        execution_count=len(entries),
        execution_interval_ns=execution_interval,
        entries=entries,
    )


def _filter_aggregation_fields(
    agg: ReportGroupAggregation,
    allowed_prefixes: set[str],
) -> ReportGroupAggregation:
    """Return a copy of *agg* keeping only fields that match *allowed_prefixes*."""
    filtered = {k: v for k, v in agg.fields.items() if any(k.startswith(p) for p in allowed_prefixes)}
    return ReportGroupAggregation(
        publish_time_ns=agg.publish_time_ns,
        execution_count=agg.execution_count,
        execution_interval_ns=agg.execution_interval_ns,
        fields=filtered,
    )


def _filter_batch_fields(
    batch: ReportGroupBatch,
    allowed_prefixes: set[str],
) -> ReportGroupBatch:
    """Return a copy of *batch* keeping only entry fields that match *allowed_prefixes*."""
    filtered_entries = [
        {k: v for k, v in entry.items() if any(k.startswith(p) for p in allowed_prefixes)} for entry in batch.entries
    ]
    return ReportGroupBatch(
        publish_time_ns=batch.publish_time_ns,
        execution_count=batch.execution_count,
        execution_interval_ns=batch.execution_interval_ns,
        entries=filtered_entries,
    )
