# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for signal_extractor module."""

from __future__ import annotations

from dataclasses import dataclass, field
from types import SimpleNamespace
from typing import TYPE_CHECKING, final
from unittest.mock import Mock, patch

import pytest
from clockwork.tools.signal_extractor.report_group_decoder import (
    ReportGroupDecodeContext,
    SignalValiditySpec,
    decode_aggregation_fields,
    decode_batch_entries,
    make_report_group_decode_context,
    make_report_group_decode_contexts,
)
from clockwork.tools.signal_extractor.signal_extractor import (
    BulkSignalData,
    ExtractedReportGroup,
    ReportGroupAggregation,
    ReportGroupBatch,
    ReportGroupSignalInfo,
    SignalExtractionFilter,
    SignalExtractor,
    StreamingReportGroup,
    _extract_aggregation_message,
    _extract_batch_message,
    _filter_aggregation_fields,
    _filter_batch_fields,
    merge_metadata,
)

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator


def _schema_field(number: int, name: str, type_id: int = 0) -> SimpleNamespace:
    return SimpleNamespace(num=number, name=name, type_id=type_id)


@final
@dataclass
class _ValidityAggregation:
    execution_count: int
    execution_interval: int
    temperature_count: int
    temperature_mean: float
    signal_presence: int
    pressure_value: float

    @classmethod
    def get_tachyon_metadata(cls) -> SimpleNamespace:
        return SimpleNamespace(
            outer_type_id=0,
            types=[
                SimpleNamespace(
                    fields=[
                        _schema_field(1, "execution_count"),
                        _schema_field(2, "execution_interval"),
                        _schema_field(7, "temperature_count"),
                        _schema_field(8, "temperature_mean"),
                        _schema_field(9, "signal_presence"),
                        _schema_field(10, "pressure_value"),
                    ]
                )
            ],
        )


@final
@dataclass
class _ValiditySoa:
    temperature_count: list[int]
    temperature_metadata: list[object]
    signal_presence: list[int]
    pressure_value: list[float]


@final
@dataclass
class _ValidityBatch:
    execution_interval: int
    signals: _ValiditySoa

    @classmethod
    def get_tachyon_metadata(cls) -> SimpleNamespace:
        return SimpleNamespace(
            outer_type_id=0,
            types=[
                SimpleNamespace(fields=[_schema_field(2, "signals", 1)]),
                SimpleNamespace(schema_type_id=2),
                SimpleNamespace(
                    fields=[
                        _schema_field(4, "temperature_count"),
                        _schema_field(5, "temperature_metadata"),
                        _schema_field(6, "signal_presence"),
                        _schema_field(7, "pressure_value"),
                    ]
                ),
            ],
        )


# --- Mock Data Factories ---


def _make_mock_report_group_signal(
    signal_index: int,
    post_agg_names: list[str] | None = None,
    alias: str | None = None,
) -> Mock:
    sig = Mock()
    sig.signal_index = signal_index
    sig.post_aggregation_types = [Mock(name=n) for n in (post_agg_names or [])]
    sig.alias = alias
    sig.validity_source = 0
    sig.validity_index = 0
    return sig


def _make_mock_report_group(
    name: str,
    signals: list[Mock],
    *,
    rg_type_name: str = "Batched",
    agg_size: int = 10,
) -> Mock:
    rg = Mock()
    rg.name = name
    rg.log_type = Mock(name="telemetry")
    rg.report_group_type = Mock(name=rg_type_name)
    rg.aggregation_size = agg_size
    rg.min_duration = 100_000_000
    rg.max_duration = 200_000_000
    rg.signals = signals
    return rg


def _make_mock_metadata() -> Mock:
    """Create a complete mock SignalMetadataConfig for testing."""
    # Signals
    sig0 = Mock()
    sig0.name = "temperature"
    sig0.pre_aggregation_type = [Mock(name="Value")]
    sig0.signal_instance_indexes = [0, 1]

    sig1 = Mock()
    sig1.name = "pressure"
    sig1.pre_aggregation_type = [Mock(name="Min"), Mock(name="Max")]
    sig1.signal_instance_indexes = [2]

    # Report group signals
    rg_sig0 = _make_mock_report_group_signal(0, ["Min", "Max"])
    rg_sig1 = _make_mock_report_group_signal(1, alias="p_sensor")

    # Report groups
    rg0 = _make_mock_report_group("health_metrics", [rg_sig0, rg_sig1])
    rg1 = _make_mock_report_group("diagnostics", [rg_sig0], rg_type_name="Aggregated", agg_size=5)

    # Cog classes
    cog0 = Mock()
    cog0.cog_path = "class-path-001"
    cog0.report_groups = [rg0, rg1]

    cog1 = Mock()
    cog1.cog_path = "class-path-002"
    cog1.report_groups = [rg0]

    # Report group instances
    rgi0 = Mock()
    rgi0.report_group_index = 0
    rgi0.channel_name = "/signals/inst0/health_metrics"
    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    rgi0.signal_instances = []

    rgi1 = Mock()
    rgi1.report_group_index = 1
    rgi1.channel_name = "/signals/inst0/diagnostics"
    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    rgi1.signal_instances = []

    # Cog instances
    inst0 = Mock()
    inst0.cog_path = "class-path-001"
    inst0.cog_instance_path = "instance-path-001"
    inst0.report_group_instances = [rgi0, rgi1]

    inst1 = Mock()
    inst1.cog_path = "class-path-001"
    inst1.cog_instance_path = "instance-path-002"
    inst1.report_group_instances = [rgi0]

    inst2 = Mock()
    inst2.cog_path = "class-path-002"
    inst2.cog_instance_path = "instance-path-003"
    inst2.report_group_instances = [rgi0]

    # Top-level metadata
    metadata = Mock()
    metadata.signal_instance_names = ["temp_left", "temp_right", "pressure_main"]
    metadata.signals = [sig0, sig1]
    metadata.cogs = [cog0, cog1]
    metadata.cog_instances = [inst0, inst1, inst2]
    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    metadata.report_group_channels = []

    return metadata


@pytest.fixture()
def mock_extractor() -> SignalExtractor:
    """Create a SignalExtractor with mocked log reading (single metadata channel)."""
    metadata = _make_mock_metadata()
    mock_message = Mock()
    mock_message.message = metadata

    with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
        reader = Mock()
        mock_cls.return_value = reader
        # Only the base channel (no suffix) is present in this log.
        reader.try_get_topic_metadata.side_effect = lambda ch: ch == "/clockwork/signal_metadata"
        reader.messages.side_effect = lambda: iter([mock_message])
        extractor = SignalExtractor("test_log.clog")
        # Force metadata load while LogReader is mocked.
        extractor._ensure_metadata_loaded()
    return extractor


# --- Filter Tests ---


class TestSignalExtractionFilter:
    """Tests for the SignalExtractionFilter dataclass."""

    def test_default_filter_has_no_constraints(self) -> None:
        """Test that default filter has all None fields."""
        f = SignalExtractionFilter()
        assert f.cog_paths is None
        assert f.cog_instance_paths is None
        assert f.report_group_names is None
        assert f.signal_ids is None
        assert f.signal_instance_ids is None
        assert f.start_time_ns is None
        assert f.end_time_ns is None

    def test_filter_with_all_fields(self) -> None:
        """Test filter construction with all fields specified."""
        f = SignalExtractionFilter(
            cog_paths=["c1"],
            cog_instance_paths=["i1"],
            report_group_names=["rg1"],
            signal_ids=["s1"],
            signal_instance_ids=["si1"],
            start_time_ns=1000,
            end_time_ns=2000,
        )
        assert f.cog_paths == ["c1"]
        assert f.start_time_ns == 1000
        assert f.end_time_ns == 2000


# --- Data Structure Tests ---


class TestDataStructures:
    """Tests for extracted data structures."""

    def test_report_group_aggregation(self) -> None:
        """Test ReportGroupAggregation construction."""
        agg = ReportGroupAggregation(
            publish_time_ns=1_000_000,
            execution_count=5,
            execution_interval_ns=500_000,
            fields={"signal_a_value_min": 1.0, "signal_a_value_max": 10.0},
        )
        assert agg.execution_count == 5
        assert agg.fields["signal_a_value_min"] == 1.0

    def test_report_group_batch(self) -> None:
        """Test ReportGroupBatch construction."""
        batch = ReportGroupBatch(
            publish_time_ns=1_000_000,
            execution_count=3,
            execution_interval_ns=300_000,
            entries=[
                {"temperature_value": 25.0},
                {"temperature_value": 26.0},
                {"temperature_value": 27.0},
            ],
        )
        assert batch.execution_count == 3
        assert len(batch.entries) == 3

    def test_report_group_signal_info(self) -> None:
        """Test ReportGroupSignalInfo construction."""
        info = ReportGroupSignalInfo(
            signal_name="temperature",
            signal_index=0,
            alias="temp_alias",
            pre_aggregation_types=["Value"],
            post_aggregation_types=["Min", "Max"],
        )
        assert info.signal_name == "temperature"
        assert info.alias == "temp_alias"

    def test_extracted_report_group(self) -> None:
        """Test ExtractedReportGroup construction."""
        erg = ExtractedReportGroup(
            report_group_name="health_metrics",
            cog_path="class-001",
            cog_instance_path="instance-001",
            channel_name="/signals/health",
            log_type="telemetry",
            report_group_type="Batched",
            aggregation_size=10,
            min_duration_ns=100_000_000,
            max_duration_ns=200_000_000,
            signal_info=[],
            data=[],
        )
        assert erg.report_group_name == "health_metrics"

    def test_bulk_signal_data(self) -> None:
        """Test BulkSignalData construction with indexes."""
        erg = ExtractedReportGroup(
            report_group_name="health",
            cog_path="class-001",
            cog_instance_path="instance-001",
            channel_name="/signals/health",
            log_type="telemetry",
            report_group_type="Batched",
            aggregation_size=10,
            min_duration_ns=100_000_000,
            max_duration_ns=200_000_000,
            signal_info=[],
            data=[],
        )
        bsd = BulkSignalData(
            report_groups=[erg],
            by_cog_path={"class-001": [erg]},
            by_cog_instance_path={"instance-001": [erg]},
        )
        assert len(bsd.report_groups) == 1
        assert "class-001" in bsd.by_cog_path
        assert "instance-001" in bsd.by_cog_instance_path


# --- SignalExtractor Metadata Tests ---


class TestSignalExtractorMetadata:
    """Tests for SignalExtractor metadata reading and query methods."""

    def test_get_metadata(self, mock_extractor: SignalExtractor) -> None:
        """Test that get_metadata returns the loaded metadata."""
        metadata = mock_extractor.get_metadata()
        assert len(metadata.signals) == 2
        assert len(metadata.cogs) == 2

    def test_no_metadata_channels_raises(self) -> None:
        """Test that ValueError is raised when no metadata channels exist."""
        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_reader = Mock()
            mock_cls.return_value = mock_reader
            mock_reader.try_get_topic_metadata.return_value = None
            # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
            mock_reader.messages.return_value = []

            extractor = SignalExtractor("test_log.clog")
            with pytest.raises(ValueError, match="No signal metadata"):
                extractor.get_metadata()

    def test_empty_metadata_channel_raises(self) -> None:
        """Test that ValueError is raised when metadata channel is empty."""
        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_reader = Mock()
            mock_cls.return_value = mock_reader
            mock_reader.try_get_topic_metadata.return_value = True
            mock_reader.messages.return_value = iter([])

            extractor = SignalExtractor("test_log.clog")
            with pytest.raises(ValueError, match="No signal metadata"):
                extractor.get_metadata()

    def test_list_cog_paths(self, mock_extractor: SignalExtractor) -> None:
        """Test listing all cog class paths."""
        paths = mock_extractor.list_cog_paths()
        assert set(paths) == {"class-path-001", "class-path-002"}

    def test_list_all_cog_instance_paths(self, mock_extractor: SignalExtractor) -> None:
        """Test listing all cog instance paths."""
        instances = mock_extractor.list_cog_instance_paths()
        assert set(instances) == {"instance-path-001", "instance-path-002", "instance-path-003"}

    def test_list_cog_instance_paths_filtered_by_class(self, mock_extractor: SignalExtractor) -> None:
        """Test listing cog instance paths filtered by class path."""
        instances = mock_extractor.list_cog_instance_paths("class-path-001")
        assert set(instances) == {"instance-path-001", "instance-path-002"}

        instances = mock_extractor.list_cog_instance_paths("class-path-002")
        assert instances == ["instance-path-003"]

    def test_list_cog_instance_paths_unknown_class(self, mock_extractor: SignalExtractor) -> None:
        """Test listing cog instance paths for unknown class returns empty."""
        instances = mock_extractor.list_cog_instance_paths("nonexistent-class")
        assert instances == []

    def test_list_all_report_groups(self, mock_extractor: SignalExtractor) -> None:
        """Test listing all report groups."""
        rgs = mock_extractor.list_report_groups()
        assert set(rgs) == {"health_metrics", "diagnostics"}

    def test_list_report_groups_for_instance(self, mock_extractor: SignalExtractor) -> None:
        """Test listing report groups for a specific cog instance."""
        rgs = mock_extractor.list_report_groups("instance-path-001")
        assert set(rgs) == {"health_metrics", "diagnostics"}

        rgs = mock_extractor.list_report_groups("instance-path-002")
        assert rgs == ["health_metrics"]

    def test_list_report_groups_unknown_instance(self, mock_extractor: SignalExtractor) -> None:
        """Test listing report groups for unknown instance returns empty."""
        rgs = mock_extractor.list_report_groups("nonexistent-instance")
        assert rgs == []

    def test_list_all_signals(self, mock_extractor: SignalExtractor) -> None:
        """Test listing all signals."""
        signals = mock_extractor.list_signals()
        assert signals == ["temperature", "pressure"]

    def test_list_signals_for_report_group(self, mock_extractor: SignalExtractor) -> None:
        """Test listing signals filtered by report group."""
        signals = mock_extractor.list_signals("health_metrics")
        assert set(signals) == {"temperature", "pressure"}

        signals = mock_extractor.list_signals("diagnostics")
        assert signals == ["temperature"]

    def test_list_signals_unknown_report_group(self, mock_extractor: SignalExtractor) -> None:
        """Test listing signals for unknown report group returns empty."""
        signals = mock_extractor.list_signals("nonexistent-rg")
        assert signals == []

    def test_list_signal_instance_names(self, mock_extractor: SignalExtractor) -> None:
        """Test listing all signal instance names."""
        names = mock_extractor.list_signal_instance_names()
        assert names == ["temp_left", "temp_right", "pressure_main"]


# --- Multi-Channel Test Helpers ---


def _make_mock_metadata_channel_2() -> Mock:  # noqa: PLR0915 For testing only
    """Create a second channel's SignalMetadataConfig for multi-channel merge testing.

    Local indexes differ from channel 1:
    - ``signal_instance_names``: ``["temp_left", "humidity_main", "temp_cabin"]``
      where ``"temp_left"`` is shared with channel 1 at a different local index.
    - ``signals[0]``: ``"humidity"`` (new), ``signal_instance_indexes=[1]`` (humidity_main).
    - ``signals[1]``: ``"temperature"`` (shared), ``signal_instance_indexes=[0, 2]``
      (temp_left and temp_cabin).
    - Cog classes: ``"class-path-001"`` (shared) and ``"class-path-003"`` (new).
    - Cog instances: ``"instance-path-004"`` and ``"instance-path-005"`` (all new).
    """
    # Local signal instance layout for channel 2:
    # index 0: "temp_left"  (shared with channel 1)
    # index 1: "humidity_main" (new)
    # index 2: "temp_cabin"    (new)

    sig_humidity = Mock()
    sig_humidity.name = "humidity"
    sig_humidity.pre_aggregation_type = [Mock(name="Value")]
    sig_humidity.signal_instance_indexes = [1]  # humidity_main at local idx 1

    sig_temperature = Mock()
    sig_temperature.name = "temperature"
    sig_temperature.pre_aggregation_type = [Mock(name="Value")]
    sig_temperature.signal_instance_indexes = [0, 2]  # temp_left, temp_cabin at local idx 0, 2

    # Report group signals using LOCAL signal indexes.
    rg_sig_humidity = _make_mock_report_group_signal(0)  # "humidity"    at local idx 0
    rg_sig_humidity.validity_source = 2
    rg_sig_humidity.validity_index = 3
    rg_sig_temperature = _make_mock_report_group_signal(1)  # "temperature" at local idx 1

    # class-path-001 in this channel has one report group (health_metrics only).
    rg_class1 = _make_mock_report_group("health_metrics", [rg_sig_temperature])

    # New class-path-003 has a humidity_metrics report group.
    rg_class3 = _make_mock_report_group("humidity_metrics", [rg_sig_humidity, rg_sig_temperature])

    cog_shared = Mock()
    cog_shared.cog_path = "class-path-001"
    cog_shared.report_groups = [rg_class1]

    cog_new = Mock()
    cog_new.cog_path = "class-path-003"
    cog_new.report_groups = [rg_class3]

    rgi_004 = Mock()
    rgi_004.report_group_index = 0
    rgi_004.channel_name = "/signals/inst4/health_metrics"
    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    rgi_004.signal_instances = []

    rgi_005 = Mock()
    rgi_005.report_group_index = 0
    rgi_005.channel_name = "/signals/inst5/humidity_metrics"
    # pyrefly: ignore[implicit-any-empty-container] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
    rgi_005.signal_instances = []

    inst4 = Mock()
    inst4.cog_path = "class-path-001"
    inst4.cog_instance_path = "instance-path-004"
    inst4.report_group_instances = [rgi_004]

    inst5 = Mock()
    inst5.cog_path = "class-path-003"
    inst5.cog_instance_path = "instance-path-005"
    inst5.report_group_instances = [rgi_005]

    rg_ch4 = Mock()
    rg_ch4.channel_name = "/signals/inst4/health_metrics"
    rg_ch4.cog_path = "class-path-001"
    rg_ch4.cog_instance_path = "instance-path-004"
    rg_ch4.report_group_index = 0

    rg_ch5 = Mock()
    rg_ch5.channel_name = "/signals/inst5/humidity_metrics"
    rg_ch5.cog_path = "class-path-003"
    rg_ch5.cog_instance_path = "instance-path-005"
    rg_ch5.report_group_index = 0

    metadata = Mock()
    metadata.signal_instance_names = ["temp_left", "humidity_main", "temp_cabin"]
    metadata.signals = [sig_humidity, sig_temperature]
    metadata.cogs = [cog_shared, cog_new]
    metadata.cog_instances = [inst4, inst5]
    metadata.report_group_channels = [rg_ch4, rg_ch5]

    return metadata


def _make_multi_channel_extractor() -> SignalExtractor:
    """Create a SignalExtractor that merges metadata from two channels.

    Channel 1 (base, no suffix): the standard mock metadata from
    ``_make_mock_metadata()``.
    Channel 2 (``/c1`` suffix): the second mock metadata from
    ``_make_mock_metadata_channel_2()``.
    """
    meta1 = _make_mock_metadata()
    meta2 = _make_mock_metadata_channel_2()

    msg1: Mock = Mock()
    msg1.message = meta1
    msg2: Mock = Mock()
    msg2.message = meta2

    messages_by_channel: dict[str, list[Mock]] = {
        "/clockwork/signal_metadata": [msg1],
        "/clockwork/signal_metadata/c1": [msg2],
    }

    def _make_reader(_uri: str) -> Mock:
        reader = Mock()
        added_channel: list[str] = []
        reader.try_get_topic_metadata.return_value = lambda ch: ch in messages_by_channel
        reader.add_topic.side_effect = added_channel.append
        # We want to lazily evaluate this; we need to check if a given channel has been previously added to the reader
        # via `add_topic` and return any msgs from `messages_by_channel` if it has. That is why we are wrapping this
        # in a lambda.
        reader.messages.side_effect = lambda: [
            msg for channel in added_channel for msg in messages_by_channel.get(channel, [])
        ]
        return reader

    with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
        mock_cls.side_effect = _make_reader
        extractor = SignalExtractor("test_log.clog")
        extractor._ensure_metadata_loaded()

    return extractor


# --- Message Extraction Helper Tests ---


@dataclass
class _MockAggMessage:
    """Fake post-aggregated report group tachyon message."""

    execution_count: int
    execution_interval: int
    signal_a_value_min: float
    signal_a_value_max: float


@dataclass
class _MockSoaSignals:
    """Fake VarSoa dataclass with list fields per signal."""

    temperature_value: list[float] = field(default_factory=list)
    pressure_min: list[float] = field(default_factory=list)
    temperature_value_metadata: list[object] = field(default_factory=list)


@dataclass
class _MockBatchMessage:
    """Fake batched report group tachyon message."""

    execution_interval: int
    signals: _MockSoaSignals = field(default_factory=_MockSoaSignals)


@dataclass(frozen=True)
class _MockSignalMetadata:
    """Fake preserved signal metadata."""

    sequence_number: int


class TestExtractAggregationMessage:
    """Tests for _extract_aggregation_message."""

    def test_extracts_fields(self) -> None:
        """Test extraction of post-aggregated message fields."""
        msg = _MockAggMessage(
            execution_count=5,
            execution_interval=500_000,
            signal_a_value_min=1.0,
            signal_a_value_max=10.0,
        )
        result = _extract_aggregation_message(1_000_000, msg)
        assert isinstance(result, ReportGroupAggregation)
        assert result.publish_time_ns == 1_000_000
        assert result.execution_count == 5
        assert result.execution_interval_ns == 500_000
        assert result.fields["signal_a_value_min"] == 1.0
        assert result.fields["signal_a_value_max"] == 10.0
        assert "execution_count" not in result.fields
        assert "execution_interval" not in result.fields

    def test_decodes_count_and_presence_bit_validity(self) -> None:
        """Missing signals retain their generated keys with ``None`` values."""
        message = _ValidityAggregation(2, 100, 0, 12.0, 0b1, 4.0)
        context = ReportGroupDecodeContext(
            signals=(
                SignalValiditySpec(field_prefix="temperature", source=1, index=7),
                SignalValiditySpec(field_prefix="pressure", source=2, index=0),
            )
        )

        assert decode_aggregation_fields(message, context) == {
            "temperature_count": None,
            "temperature_mean": None,
            "pressure_value": 4.0,
        }

    def test_legacy_context_keeps_default_values(self) -> None:
        """Legacy metadata preserves the historical assume-present behavior."""
        message = _ValidityAggregation(2, 100, 0, 0.0, 0, 0.0)
        context = ReportGroupDecodeContext(signals=(SignalValiditySpec(field_prefix="temperature", source=0, index=0),))

        result = decode_aggregation_fields(message, context)

        assert result["temperature_count"] == 0
        assert result["temperature_mean"] == 0.0
        assert "signal_presence" not in result

    def test_context_builder_uses_signal_name_for_none_alias(self) -> None:
        """An explicitly absent alias falls back to the signal name."""
        metadata = SimpleNamespace(signals=[SimpleNamespace(name="temperature")])
        report_group = SimpleNamespace(
            signals=[SimpleNamespace(signal_index=0, alias=None, validity_source=2, validity_index=0)]
        )
        context = make_report_group_decode_context(metadata, report_group)

        result = decode_aggregation_fields(_ValidityAggregation(2, 100, 0, 9.0, 0, 3.0), context)

        assert result["temperature_count"] is None
        assert result["temperature_mean"] is None

    def test_context_canonicalizes_overlapping_prefixes(self) -> None:
        """Direct construction cannot bypass longest-prefix ownership ordering."""
        context = ReportGroupDecodeContext(
            signals=(
                SignalValiditySpec(field_prefix="signal", source=0, index=0),
                SignalValiditySpec(field_prefix="signal_nested", source=2, index=0),
            )
        )

        assert [spec.field_prefix for spec in context.signals] == ["signal_nested", "signal"]

    def test_context_builder_rejects_invalid_signal_index(self) -> None:
        """Invalid report-group signal indexes fail with a contextual error."""
        metadata = SimpleNamespace(signals=[SimpleNamespace(name="temperature")])
        report_group = SimpleNamespace(
            signals=[SimpleNamespace(signal_index=1, alias=None, validity_source=0, validity_index=0)]
        )

        with pytest.raises(ValueError, match="Signal index 1"):
            make_report_group_decode_context(metadata, report_group)

    def test_contexts_builder_indexes_report_groups_by_channel(self) -> None:
        """The configuration-level builder resolves cog and report-group references."""
        report_group = SimpleNamespace(
            signals=[SimpleNamespace(signal_index=0, alias="temp", validity_source=2, validity_index=4)]
        )
        metadata = SimpleNamespace(
            signals=[SimpleNamespace(name="temperature")],
            cogs=[SimpleNamespace(cog_path="cog.path", report_groups=[report_group])],
            report_group_channels=[
                SimpleNamespace(channel_name="/reports/temperature", cog_path="cog.path", report_group_index=0)
            ],
        )

        contexts = make_report_group_decode_contexts(metadata)

        assert contexts == {
            "/reports/temperature": ReportGroupDecodeContext(
                signals=(SignalValiditySpec(field_prefix="temp", source=2, index=4),)
            )
        }


class TestExtractBatchMessage:
    """Tests for _extract_batch_message."""

    def test_extracts_soa_entries(self) -> None:
        """Test extraction of batched SoA entries."""
        soa = _MockSoaSignals(
            temperature_value=[25.0, 26.0, 27.0],
            pressure_min=[100.0, 101.0, 102.0],
        )
        msg = _MockBatchMessage(execution_interval=300_000, signals=soa)
        result = _extract_batch_message(2_000_000, msg)
        assert isinstance(result, ReportGroupBatch)
        assert result.publish_time_ns == 2_000_000
        assert result.execution_count == 3
        assert result.execution_interval_ns == 300_000
        assert len(result.entries) == 3
        assert result.entries[0]["temperature_value"] == 25.0
        assert result.entries[2]["pressure_min"] == 102.0

    def test_extracts_preserved_metadata_fields(self) -> None:
        """Test extraction keeps non-scalar batched metadata fields."""
        metadata = _MockSignalMetadata(sequence_number=42)
        soa = _MockSoaSignals(
            temperature_value=[25.0],
            temperature_value_metadata=[metadata],
        )
        msg = _MockBatchMessage(execution_interval=300_000, signals=soa)

        result = _extract_batch_message(2_000_000, msg)

        assert result.entries == [{"temperature_value": 25.0, "temperature_value_metadata": metadata}]

    def test_empty_soa(self) -> None:
        """Test extraction with empty SoA signals."""
        msg = _MockBatchMessage(execution_interval=0, signals=_MockSoaSignals())
        result = _extract_batch_message(0, msg)
        assert result.entries == []
        assert result.execution_count == 0

    def test_decodes_each_row_and_preserves_metadata_key(self) -> None:
        """Batch validity is evaluated independently for each signal and row."""
        metadata = object()
        message = _ValidityBatch(
            execution_interval=100,
            signals=_ValiditySoa(
                temperature_count=[0, 2],
                temperature_metadata=[metadata, metadata],
                signal_presence=[0b1, 0b0],
                pressure_value=[3.0, 4.0],
            ),
        )
        context = ReportGroupDecodeContext(
            signals=(
                SignalValiditySpec(field_prefix="temperature", source=1, index=4),
                SignalValiditySpec(field_prefix="pressure", source=2, index=0),
            )
        )

        assert decode_batch_entries(message, context) == [
            {"temperature_count": None, "temperature_metadata": None, "pressure_value": 3.0},
            {"temperature_count": 2, "temperature_metadata": metadata, "pressure_value": None},
        ]


# --- Bulk Extraction Tests ---


def _make_metadata_with_channels() -> Mock:
    """Create metadata with report_group_channels for bulk extraction tests."""
    metadata = _make_mock_metadata()

    # Add report group channels
    rg_ch0 = Mock()
    rg_ch0.channel_name = "/signals/inst0/health_metrics"
    rg_ch0.cog_path = "class-path-001"
    rg_ch0.cog_instance_path = "instance-path-001"
    rg_ch0.report_group_index = 0

    rg_ch1 = Mock()
    rg_ch1.channel_name = "/signals/inst0/diagnostics"
    rg_ch1.cog_path = "class-path-001"
    rg_ch1.cog_instance_path = "instance-path-001"
    rg_ch1.report_group_index = 1

    rg_ch2 = Mock()
    rg_ch2.channel_name = "/signals/inst2/health_metrics"
    rg_ch2.cog_path = "class-path-002"
    rg_ch2.cog_instance_path = "instance-path-003"
    rg_ch2.report_group_index = 0

    metadata.report_group_channels = [rg_ch0, rg_ch1, rg_ch2]
    return metadata


def _make_bulk_extractor(metadata: Mock) -> SignalExtractor:
    """Create a SignalExtractor with pre-loaded metadata for bulk extraction tests.

    Args:
        metadata: Mock SignalMetadataConfig.
    """
    metadata_msg = Mock()
    metadata_msg.message = metadata

    with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
        reader = Mock()
        reader.try_get_topic_metadata.return_value = True
        reader.messages.return_value = iter([metadata_msg])
        mock_cls.return_value = reader
        extractor = SignalExtractor("test_log.clog")
        extractor._ensure_metadata_loaded()

    return extractor


# --- Merge Metadata Tests ---


class TestMergeMetadata:
    """Tests for the merge_metadata function."""

    def test_single_channel_returns_equivalent_structure(self) -> None:
        """Merging a single channel produces a structure with identical data."""
        meta = _make_mock_metadata()
        result = merge_metadata([meta])
        assert [sig.name for sig in result.signals] == ["temperature", "pressure"]
        assert len(result.cogs) == 2
        assert list(result.signal_instance_names) == ["temp_left", "temp_right", "pressure_main"]

    def test_two_channels_deduplicate_signals_by_name(self) -> None:
        """Signal names shared across channels appear only once in the merged result."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        signal_names = [sig.name for sig in result.signals]
        assert set(signal_names) == {"temperature", "pressure", "humidity"}
        assert len(signal_names) == 3

    def test_two_channels_deduplicate_cog_classes(self) -> None:
        """Cog classes with the same path are not duplicated in the merged result."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        class_paths = [str(c.cog_path) for c in result.cogs]
        assert len(class_paths) == len(set(class_paths))
        assert set(class_paths) == {"class-path-001", "class-path-002", "class-path-003"}

    def test_two_channels_concatenate_cog_instances(self) -> None:
        """All cog instances from every channel are included in the merged result."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        instance_paths = {str(inst.cog_instance_path) for inst in result.cog_instances}
        assert instance_paths == {
            "instance-path-001",
            "instance-path-002",
            "instance-path-003",
            "instance-path-004",
            "instance-path-005",
        }

    def test_signal_instance_names_deduplicated(self) -> None:
        """Instance names shared between channels appear only once."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        assert result.signal_instance_names.count("temp_left") == 1
        assert set(result.signal_instance_names) == {
            "temp_left",
            "temp_right",
            "pressure_main",
            "humidity_main",
            "temp_cabin",
        }

    def test_shared_signal_instance_indexes_merged(self) -> None:
        """A signal shared across channels has its instance indexes unioned."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        temperature_sig = next(sig for sig in result.signals if sig.name == "temperature")
        index_names = {result.signal_instance_names[i] for i in temperature_sig.signal_instance_indexes}
        # Channel 1 contributes temp_left, temp_right; channel 2 adds temp_cabin.
        assert "temp_left" in index_names
        assert "temp_right" in index_names
        assert "temp_cabin" in index_names

    def test_report_group_signal_indexes_remapped(self) -> None:
        """Signal indexes inside merged report groups reference the merged signals array."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        # class-path-003 came from channel 2 where humidity=local idx 0, temperature=local idx 1.
        cog_class3 = next(c for c in result.cogs if str(c.cog_path) == "class-path-003")
        rg = cog_class3.report_groups[0]  # humidity_metrics
        for rg_sig in rg.signals:
            assert 0 <= rg_sig.signal_index < len(result.signals)
            assert result.signals[rg_sig.signal_index].name in {"humidity", "temperature"}

    def test_report_group_channels_concatenated(self) -> None:
        """Report group channels from all channels are concatenated."""
        meta1 = _make_mock_metadata()
        meta2 = _make_mock_metadata_channel_2()
        result = merge_metadata([meta1, meta2])
        channel_names = {ch.channel_name for ch in result.report_group_channels}
        # channel 1 base mock has no report_group_channels; channel 2 adds two.
        assert "/signals/inst4/health_metrics" in channel_names
        assert "/signals/inst5/humidity_metrics" in channel_names

    def test_report_group_validity_metadata_is_preserved(self) -> None:
        """Multi-channel merging retains each report-group signal's validity contract."""
        result = merge_metadata([_make_mock_metadata(), _make_mock_metadata_channel_2()])
        cog_class3 = next(cog for cog in result.cogs if str(cog.cog_path) == "class-path-003")
        humidity_signal = cog_class3.report_groups[0].signals[0]

        assert humidity_signal.validity_source == 2
        assert humidity_signal.validity_index == 3


# --- Multi-Channel SignalExtractor Tests ---


class TestMultiChannelMerge:
    """Tests for SignalExtractor when merging metadata from multiple channels."""

    def test_list_signals_across_channels(self) -> None:
        """Signals from all channels are combined without duplicates."""
        extractor = _make_multi_channel_extractor()
        signals = extractor.list_signals()
        assert set(signals) == {"temperature", "pressure", "humidity"}
        assert len(signals) == 3

    def test_list_cog_paths_across_channels(self) -> None:
        """Cog class paths from all channels are present without duplicates."""
        extractor = _make_multi_channel_extractor()
        paths = extractor.list_cog_paths()
        assert set(paths) == {"class-path-001", "class-path-002", "class-path-003"}

    def test_list_all_cog_instance_paths_across_channels(self) -> None:
        """Cog instance paths from all channels are aggregated into one list."""
        extractor = _make_multi_channel_extractor()
        instances = extractor.list_cog_instance_paths()
        assert set(instances) == {
            "instance-path-001",
            "instance-path-002",
            "instance-path-003",
            "instance-path-004",
            "instance-path-005",
        }

    def test_list_cog_instance_paths_for_shared_class_spans_channels(self) -> None:
        """list_cog_instance_paths for a class present in multiple channels returns all instances."""
        extractor = _make_multi_channel_extractor()
        # class-path-001 has instances 001/002 (channel 1) and 004 (channel 2).
        instances = extractor.list_cog_instance_paths("class-path-001")
        assert set(instances) == {"instance-path-001", "instance-path-002", "instance-path-004"}

    def test_list_signal_instance_names_deduped(self) -> None:
        """Signal instance names shared across channels appear only once."""
        extractor = _make_multi_channel_extractor()
        names = extractor.list_signal_instance_names()
        assert set(names) == {"temp_left", "temp_right", "pressure_main", "humidity_main", "temp_cabin"}
        assert len(names) == 5

    def test_report_groups_from_new_class_accessible(self) -> None:
        """Report groups from a cog class that only exists in one channel are accessible."""
        extractor = _make_multi_channel_extractor()
        rgs = extractor.list_report_groups("instance-path-005")
        assert rgs == ["humidity_metrics"]

    def test_list_all_report_groups_includes_new_class(self) -> None:
        """list_report_groups without filter returns report groups from all channels."""
        extractor = _make_multi_channel_extractor()
        rgs = extractor.list_report_groups()
        assert "humidity_metrics" in rgs
        assert "health_metrics" in rgs
        assert "diagnostics" in rgs


def _make_topic_aware_reader_factory(all_msgs: list[Mock]) -> Callable[[str], Mock]:
    """Create a factory that returns topic-aware mock LogReaders.

    Each call to the returned factory produces a fresh Mock LogReader.
    When ``add_topic`` is called on the reader the topic is recorded.
    Calling ``messages()`` yields only those messages whose ``.topic``
    matches a previously added topic.
    """

    def _factory(_uri: str) -> Mock:
        reader = Mock()
        added_topics: list[str] = []
        reader.try_get_topic_metadata.return_value = True
        reader.add_topic.side_effect = added_topics.append

        def _yield_matching() -> Iterator[Mock]:
            return iter([m for m in all_msgs if m.topic in added_topics])

        reader.messages = _yield_matching
        return reader

    return _factory


class TestBulkExtract:
    """Tests for SignalExtractor.bulk_extract."""

    def test_empty_when_no_channels(self, mock_extractor: SignalExtractor) -> None:
        """Test bulk_extract returns empty when no channels match."""
        # mock_extractor has empty report_group_channels
        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_reader = Mock()
            mock_cls.return_value = mock_reader
            mock_reader.try_get_topic_metadata.return_value = True
            mock_reader.messages.return_value = iter([])

            result = mock_extractor.bulk_extract()
        assert result.report_groups == []
        assert result.by_cog_path == {}

    def test_extracts_aggregated_channel(self) -> None:
        """Test bulk extraction of an aggregated report group."""
        metadata = _make_metadata_with_channels()

        agg_msg = _MockAggMessage(
            execution_count=10,
            execution_interval=1_000_000,
            signal_a_value_min=5.0,
            signal_a_value_max=50.0,
        )

        extractor = _make_bulk_extractor(metadata)

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            data_reader = Mock()
            mock_cls.return_value = data_reader
            data_reader.try_get_topic_metadata.return_value = True

            data_msg = Mock()
            data_msg.topic = "/signals/inst0/diagnostics"
            data_msg.publish_time = Mock()
            data_msg.publish_time.nanoseconds = 100_000
            data_msg.message = agg_msg
            data_reader.messages.return_value = iter([data_msg])

            result = extractor.bulk_extract(
                SignalExtractionFilter(report_group_names=["diagnostics"]),
            )

        assert len(result.report_groups) == 1
        rg = result.report_groups[0]
        assert rg.report_group_name == "diagnostics"
        assert rg.cog_path == "class-path-001"
        assert rg.cog_instance_path == "instance-path-001"
        assert len(rg.data) == 1
        assert isinstance(rg.data[0], ReportGroupAggregation)
        assert rg.data[0].execution_count == 10
        assert rg.data[0].fields["signal_a_value_min"] == 5.0

    def test_extracts_batched_channel(self) -> None:
        """Test bulk extraction of a batched report group."""
        metadata = _make_metadata_with_channels()

        soa = _MockSoaSignals(
            temperature_value=[20.0, 21.0],
            pressure_min=[90.0, 91.0],
        )
        batch_msg = _MockBatchMessage(execution_interval=200_000, signals=soa)

        extractor = _make_bulk_extractor(metadata)

        data_msg = Mock()
        data_msg.topic = "/signals/inst0/health_metrics"
        data_msg.publish_time = Mock()
        data_msg.publish_time.nanoseconds = 200_000
        data_msg.message = batch_msg

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_cls.side_effect = _make_topic_aware_reader_factory([data_msg])

            result = extractor.bulk_extract(
                SignalExtractionFilter(report_group_names=["health_metrics"]),
            )

        assert len(result.report_groups) == 1
        rg = result.report_groups[0]
        assert rg.report_group_name == "health_metrics"
        assert len(rg.data) == 1
        assert isinstance(rg.data[0], ReportGroupBatch)
        assert rg.data[0].execution_count == 2
        assert rg.data[0].entries[0]["temperature_value"] == 20.0

    def test_filter_by_cog_path(self) -> None:
        """Test bulk extraction filtered by cog class path."""
        metadata = _make_metadata_with_channels()

        extractor = _make_bulk_extractor(metadata)

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            data_reader = Mock()
            mock_cls.return_value = data_reader
            data_reader.try_get_topic_metadata.return_value = True
            data_reader.messages.return_value = iter([])

            result = extractor.bulk_extract(
                SignalExtractionFilter(cog_paths=["class-path-002"]),
            )

        # Only class-002 channels should be included, even if empty
        assert result.report_groups == []

    def test_filter_by_cog_instance_path(self) -> None:
        """Test bulk extraction filtered by cog instance path."""
        metadata = _make_metadata_with_channels()

        extractor = _make_bulk_extractor(metadata)

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            data_reader = Mock()
            mock_cls.return_value = data_reader
            data_reader.try_get_topic_metadata.return_value = True
            data_reader.messages.return_value = iter([])

            result = extractor.bulk_extract(
                SignalExtractionFilter(cog_instance_paths=["instance-path-001"]),
            )

        assert result.report_groups == []

    def test_time_filtering(self) -> None:
        """Test bulk extraction with time range filter."""
        metadata = _make_metadata_with_channels()

        agg_msg_early = _MockAggMessage(
            execution_count=1, execution_interval=100, signal_a_value_min=1.0, signal_a_value_max=2.0
        )
        agg_msg_late = _MockAggMessage(
            execution_count=2, execution_interval=200, signal_a_value_min=3.0, signal_a_value_max=4.0
        )

        extractor = _make_bulk_extractor(metadata)

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            data_reader = Mock()
            mock_cls.return_value = data_reader
            data_reader.try_get_topic_metadata.return_value = True

            early_msg = Mock()
            early_msg.topic = "/signals/inst0/diagnostics"
            early_msg.publish_time = Mock()
            early_msg.publish_time.nanoseconds = 1000
            early_msg.message = agg_msg_early

            late_msg = Mock()
            late_msg.topic = "/signals/inst0/diagnostics"
            late_msg.publish_time = Mock()
            late_msg.publish_time.nanoseconds = 5000
            late_msg.message = agg_msg_late

            data_reader.messages.return_value = iter([early_msg, late_msg])

            result = extractor.bulk_extract(
                SignalExtractionFilter(
                    report_group_names=["diagnostics"],
                    start_time_ns=2000,
                    end_time_ns=6000,
                ),
            )

        assert len(result.report_groups) == 1
        assert len(result.report_groups[0].data) == 1
        assert result.report_groups[0].data[0].execution_count == 2

    def test_indexes_populated(self) -> None:
        """Test that by_cog_path and by_cog_instance_path indexes are populated."""
        metadata = _make_metadata_with_channels()

        agg_msg = _MockAggMessage(
            execution_count=1, execution_interval=100, signal_a_value_min=1.0, signal_a_value_max=2.0
        )

        extractor = _make_bulk_extractor(metadata)

        all_msgs: list[Mock] = []
        for ch_name in ["/signals/inst0/diagnostics", "/signals/inst2/health_metrics"]:
            m = Mock()
            m.topic = ch_name
            m.publish_time = Mock()
            m.publish_time.nanoseconds = 1000
            m.message = agg_msg
            all_msgs.append(m)

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_cls.side_effect = _make_topic_aware_reader_factory(all_msgs)

            result = extractor.bulk_extract()

        assert len(result.report_groups) == 2
        assert "class-path-001" in result.by_cog_path
        assert "class-path-002" in result.by_cog_path
        assert "instance-path-001" in result.by_cog_instance_path
        assert "instance-path-003" in result.by_cog_instance_path


# --- Streaming Tests ---


class TestStreamingReportGroup:
    """Tests for the StreamingReportGroup class."""

    def test_construction_and_metadata(self) -> None:
        """Test StreamingReportGroup exposes metadata fields."""
        rg = StreamingReportGroup(
            report_group_name="health_metrics",
            cog_path="class-001",
            cog_instance_path="instance-001",
            channel_name="/signals/health",
            log_type="telemetry",
            report_group_type="Batched",
            aggregation_size=10,
            min_duration_ns=100_000_000,
            max_duration_ns=200_000_000,
            signal_info=[],
            batch_factory=lambda: iter([]),
        )
        assert rg.report_group_name == "health_metrics"
        assert rg.cog_path == "class-001"
        assert rg.cog_instance_path == "instance-001"
        assert rg.aggregation_size == 10

    def test_batches_delegates_to_factory(self) -> None:
        """Test batches() yields items from the factory callable."""
        expected = ReportGroupAggregation(
            publish_time_ns=1000,
            execution_count=1,
            execution_interval_ns=100,
            fields={"a_v": 1.0},
        )

        rg = StreamingReportGroup(
            report_group_name="rg",
            cog_path="c",
            cog_instance_path="i",
            channel_name="/ch",
            log_type="telemetry",
            report_group_type="Aggregated",
            aggregation_size=5,
            min_duration_ns=0,
            max_duration_ns=0,
            signal_info=[],
            batch_factory=lambda: iter([expected]),
        )
        result = list(rg.batches())
        assert len(result) == 1
        assert result[0] is expected

    def test_batches_can_be_called_multiple_times(self) -> None:
        """Test that each batches() call starts a fresh iterator."""
        call_count = 0

        def _factory() -> Iterator[ReportGroupAggregation]:
            nonlocal call_count
            call_count += 1
            return iter(
                [
                    ReportGroupAggregation(
                        publish_time_ns=1000,
                        execution_count=1,
                        execution_interval_ns=100,
                        fields={},
                    ),
                ]
            )

        rg = StreamingReportGroup(
            report_group_name="rg",
            cog_path="c",
            cog_instance_path="i",
            channel_name="/ch",
            log_type="telemetry",
            report_group_type="Aggregated",
            aggregation_size=5,
            min_duration_ns=0,
            max_duration_ns=0,
            signal_info=[],
            batch_factory=_factory,
        )
        first = list(rg.batches())
        second = list(rg.batches())
        assert len(first) == 1
        assert len(second) == 1
        assert call_count == 2


class TestStream:
    """Tests for SignalExtractor.stream."""

    def test_stream_yields_streaming_report_groups(self) -> None:
        """Test that stream yields StreamingReportGroups with correct metadata."""
        metadata = _make_metadata_with_channels()
        extractor = _make_bulk_extractor(metadata)

        # stream() only resolves metadata; no LogReader needed yet.
        report_groups = list(
            extractor.stream(SignalExtractionFilter(report_group_names=["diagnostics"])),
        )

        assert len(report_groups) == 1
        rg = report_groups[0]
        assert isinstance(rg, StreamingReportGroup)
        assert rg.report_group_name == "diagnostics"
        assert rg.cog_path == "class-path-001"
        assert rg.cog_instance_path == "instance-path-001"
        assert rg.aggregation_size == 5

    def test_stream_batches_lazy_reading(self) -> None:
        """Test that data is not read until batches() is called."""
        metadata = _make_metadata_with_channels()
        extractor = _make_bulk_extractor(metadata)

        # Collecting stream results does NOT open any data reader.
        report_groups = list(
            extractor.stream(SignalExtractionFilter(report_group_names=["diagnostics"])),
        )
        assert len(report_groups) == 1

        # Now call batches(), which should open a reader.
        agg_msg = _MockAggMessage(
            execution_count=5,
            execution_interval=100,
            signal_a_value_min=1.0,
            signal_a_value_max=2.0,
        )
        msg = Mock()
        msg.topic = "/signals/inst0/diagnostics"
        msg.publish_time = Mock()
        msg.publish_time.nanoseconds = 100_000
        msg.message = agg_msg

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_cls.side_effect = _make_topic_aware_reader_factory([msg])
            batches = list(report_groups[0].batches())

        assert len(batches) == 1
        assert isinstance(batches[0], ReportGroupAggregation)
        assert batches[0].execution_count == 5

    def test_stream_filters_signal_info_by_signal_ids(self) -> None:
        """Test that signal_info is filtered when signal_ids filter is set."""
        metadata = _make_metadata_with_channels()
        extractor = _make_bulk_extractor(metadata)

        report_groups = list(
            extractor.stream(SignalExtractionFilter(signal_ids=["temperature"])),
        )

        for rg in report_groups:
            if rg.report_group_name == "health_metrics":
                assert len(rg.signal_info) == 1
                assert rg.signal_info[0].signal_name == "temperature"

    def test_stream_time_filtering(self) -> None:
        """Test that time filters are applied during streaming."""
        metadata = _make_metadata_with_channels()
        extractor = _make_bulk_extractor(metadata)

        early = _MockAggMessage(
            execution_count=1,
            execution_interval=100,
            signal_a_value_min=0.0,
            signal_a_value_max=0.0,
        )
        late = _MockAggMessage(
            execution_count=2,
            execution_interval=200,
            signal_a_value_min=0.0,
            signal_a_value_max=0.0,
        )

        msgs: list[Mock] = []
        for ts, payload in [(1000, early), (5000, late)]:
            m = Mock()
            m.topic = "/signals/inst0/diagnostics"
            m.publish_time = Mock()
            m.publish_time.nanoseconds = ts
            m.message = payload
            msgs.append(m)

        report_groups = list(
            extractor.stream(
                SignalExtractionFilter(
                    report_group_names=["diagnostics"],
                    start_time_ns=2000,
                    end_time_ns=6000,
                ),
            ),
        )
        assert len(report_groups) == 1

        with patch("clockwork.tools.signal_extractor.signal_extractor.LogReader") as mock_cls:
            mock_cls.side_effect = _make_topic_aware_reader_factory(msgs)
            batches = list(report_groups[0].batches())

        assert len(batches) == 1
        assert batches[0].execution_count == 2

    def test_stream_all_channels_without_filter(self) -> None:
        """Test that stream yields all channels when no filter is given."""
        metadata = _make_metadata_with_channels()
        extractor = _make_bulk_extractor(metadata)

        report_groups = list(extractor.stream())

        # 3 channels in the metadata → 3 streaming report groups.
        assert len(report_groups) == 3
        names = {rg.report_group_name for rg in report_groups}
        assert names == {"health_metrics", "diagnostics"}


# --- Field Filtering Tests ---


class TestFilterAggregationFields:
    """Tests for _filter_aggregation_fields."""

    def test_keeps_matching_fields(self) -> None:
        """Test that matching fields are kept and others removed."""
        agg = ReportGroupAggregation(
            publish_time_ns=1000,
            execution_count=5,
            execution_interval_ns=100,
            fields={"temperature_value_min": 1.0, "temperature_value_max": 10.0, "pressure_min_mean": 5.0},
        )
        result = _filter_aggregation_fields(agg, {"temperature_"})
        assert "temperature_value_min" in result.fields
        assert "temperature_value_max" in result.fields
        assert "pressure_min_mean" not in result.fields
        assert result.execution_count == 5

    def test_empty_prefixes_removes_all(self) -> None:
        """Test that an empty prefix set removes all fields."""
        agg = ReportGroupAggregation(
            publish_time_ns=1000,
            execution_count=1,
            execution_interval_ns=0,
            fields={"a_v": 1.0},
        )
        result = _filter_aggregation_fields(agg, set())
        assert result.fields == {}


class TestFilterBatchFields:
    """Tests for _filter_batch_fields."""

    def test_keeps_matching_entry_fields(self) -> None:
        """Test that matching entry fields are kept and others removed."""
        batch = ReportGroupBatch(
            publish_time_ns=1000,
            execution_count=2,
            execution_interval_ns=100,
            entries=[
                {"temperature_value": 25.0, "pressure_min": 100.0},
                {"temperature_value": 26.0, "pressure_min": 101.0},
            ],
        )
        result = _filter_batch_fields(batch, {"temperature_"})
        assert len(result.entries) == 2
        assert result.entries[0] == {"temperature_value": 25.0}
        assert result.entries[1] == {"temperature_value": 26.0}

    def test_empty_prefixes_removes_all_entry_fields(self) -> None:
        """Test that an empty prefix set removes all entry fields."""
        batch = ReportGroupBatch(
            publish_time_ns=1000,
            execution_count=1,
            execution_interval_ns=0,
            entries=[{"a_v": 1.0}],
        )
        result = _filter_batch_fields(batch, set())
        assert result.entries == [{}]


# --- Signal Instance ID Filter Tests ---


class TestSignalInstanceFilter:
    """Tests for signal_instance_ids filtering via _resolve_signal_index_filter."""

    def test_filter_by_signal_instance_ids(self, mock_extractor: SignalExtractor) -> None:
        """Test _resolve_signal_index_filter with signal_instance_ids."""
        # "temp_left" is instance index 0, belongs to signal 0 ("temperature")
        f = SignalExtractionFilter(signal_instance_ids=["temp_left"])
        result = mock_extractor._resolve_signal_index_filter(f)
        assert result == {0}  # signal index for "temperature"

    def test_filter_by_signal_instance_ids_another(self, mock_extractor: SignalExtractor) -> None:
        """Test filtering by a different signal instance."""
        # "pressure_main" is instance index 2, belongs to signal 1 ("pressure")
        f = SignalExtractionFilter(signal_instance_ids=["pressure_main"])
        result = mock_extractor._resolve_signal_index_filter(f)
        assert result == {1}  # signal index for "pressure"

    def test_combined_signal_ids_and_instance_ids(self, mock_extractor: SignalExtractor) -> None:
        """Test AND logic when both signal_ids and signal_instance_ids are set."""
        # signal_ids=["temperature"] → signal index {0}
        # signal_instance_ids=["pressure_main"] → signal index {1}
        # AND → empty
        f = SignalExtractionFilter(signal_ids=["temperature"], signal_instance_ids=["pressure_main"])
        result = mock_extractor._resolve_signal_index_filter(f)
        assert result == set()

    def test_combined_matching_signal_ids_and_instance_ids(self, mock_extractor: SignalExtractor) -> None:
        """Test AND logic when both filters match the same signal."""
        # signal_ids=["temperature"] → {0}
        # signal_instance_ids=["temp_left"] → {0}
        # AND → {0}
        f = SignalExtractionFilter(signal_ids=["temperature"], signal_instance_ids=["temp_left"])
        result = mock_extractor._resolve_signal_index_filter(f)
        assert result == {0}

    def test_unknown_instance_returns_empty(self, mock_extractor: SignalExtractor) -> None:
        """Test that unknown signal instance names result in empty filter."""
        f = SignalExtractionFilter(signal_instance_ids=["nonexistent_instance"])
        result = mock_extractor._resolve_signal_index_filter(f)
        assert result == set()
