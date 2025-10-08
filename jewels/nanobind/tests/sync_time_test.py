# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

# pyright: reportPrivateUsage=false
"""Tests for SyncTime and Duration classes."""

import pickle

import pytest
from hypothesis import example, given
from hypothesis import strategies as st
from jewels.nanobind.nb_sync_time import Duration, SyncTime


@example(seconds=0.0)
@example(seconds=1.0)
@given(seconds=st.floats(min_value=-1e6, max_value=1e6, allow_nan=False, allow_infinity=False))
def test_construction_seconds(seconds: float) -> None:
    """Test that construction from seconds preserves precision within reasonable bounds."""
    abstol = 1e-8
    time = SyncTime(seconds=seconds)
    assert time.to_s() == pytest.approx(seconds, abs=abstol)

    duration = Duration(seconds=seconds)
    assert duration.to_s() == pytest.approx(seconds, abs=abstol)


@example(nanoseconds=0)
@example(nanoseconds=int(1e9))
@given(nanoseconds=st.integers(min_value=-(2**63), max_value=(2**63 - 1)))
def test_construction_nanoseconds(nanoseconds: int) -> None:
    """Test that construction from nanoseconds is exact."""
    time = SyncTime(nanoseconds=nanoseconds)
    assert time.to_ns() == nanoseconds

    duration = Duration(nanoseconds=nanoseconds)
    assert duration.to_ns() == nanoseconds


@example(seconds=0.0, nanoseconds=0)
@example(seconds=1.0, nanoseconds=int(5e8))
@given(
    seconds=st.floats(min_value=-1e6, max_value=1e6, allow_nan=False, allow_infinity=False),
    nanoseconds=st.integers(min_value=-int(1e9), max_value=int(1e9)),
)
def test_combined_construction(seconds: float, nanoseconds: int) -> None:
    """Test construction with both seconds and nanoseconds."""
    time = SyncTime(seconds=seconds, nanoseconds=nanoseconds)
    expected_ns = int(seconds * 1e9) + nanoseconds
    assert time.to_ns() == expected_ns


@example(ns1=0, ns2=0)
@example(ns1=0, ns2=1)
@example(ns1=1, ns2=0)
@example(ns1=int(1e9), ns2=int(5e8))
@given(
    ns1=st.integers(min_value=-(2**62), max_value=(2**62 - 1)),
    ns2=st.integers(min_value=-(2**62), max_value=(2**62 - 1)),
)
def test_sync_time_arithmetic_properties(ns1: int, ns2: int) -> None:
    """Test arithmetic properties of SyncTime operations."""
    time1 = SyncTime(nanoseconds=ns1)
    time2 = SyncTime(nanoseconds=ns2)

    dur = time2 - time1
    assert dur.to_ns() == ns2 - ns1

    # Commutativity of duration arithmetic
    assert time1 + dur == time2
    assert dur + time1 == time2

    # Subtraction is inverse of addition
    assert time2 - dur == time1


@example(ns1=0, ns2=0)
@example(ns1=0, ns2=1)
@example(ns1=1, ns2=0)
@example(ns1=int(1e9), ns2=int(5e8))
@given(
    ns1=st.integers(min_value=-(2**62), max_value=(2**62 - 1)),
    ns2=st.integers(min_value=-(2**62), max_value=(2**62 - 1)),
)
def test_duration_arithmetic_properties(ns1: int, ns2: int) -> None:
    """Test arithmetic properties of duration operations."""
    dur1 = Duration(nanoseconds=ns1)
    dur2 = Duration(nanoseconds=ns2)

    # addition/subtraction
    assert (dur1 + dur2).to_ns() == ns1 + ns2, f"Expected {ns1} + {ns2} = {ns1 + ns2}, got {(dur1 + dur2).to_ns()}"
    assert (dur1 - dur2).to_ns() == ns1 - ns2

    # Commutativity
    assert dur1 + dur2 == dur2 + dur1
    assert dur1 - dur2 == -(dur2 - dur1)

    # Associativity with zero
    zero = Duration(nanoseconds=0)
    assert dur1 + zero == dur1
    assert zero + dur1 == dur1

    # Inverse
    assert dur1 + (-dur1) == zero


@example(time1_ns=0, time2_ns=0)
@example(time1_ns=0, time2_ns=1)
@example(time1_ns=1, time2_ns=0)
@example(time1_ns=int(1e9), time2_ns=int(5e8))
@given(
    time1_ns=st.integers(min_value=-(2**63), max_value=(2**63 - 1)),
    time2_ns=st.integers(min_value=-(2**63), max_value=(2**63 - 1)),
)
def test_comparisons(time1_ns: int, time2_ns: int) -> None:
    """Test comparison operations."""
    sync_time1 = SyncTime(nanoseconds=time1_ns)
    sync_time2 = SyncTime(nanoseconds=time2_ns)

    assert (sync_time1 == sync_time2) == (time1_ns == time2_ns)
    assert (sync_time1 != sync_time2) == (time1_ns != time2_ns)
    assert (sync_time1 < sync_time2) == (time1_ns < time2_ns)
    assert (sync_time1 > sync_time2) == (time1_ns > time2_ns)
    assert (sync_time1 <= sync_time2) == (time1_ns <= time2_ns)
    assert (sync_time1 >= sync_time2) == (time1_ns >= time2_ns)

    duration1 = Duration(nanoseconds=time1_ns)
    duration2 = Duration(nanoseconds=time2_ns)

    assert (duration1 == duration2) == (time1_ns == time2_ns)
    assert (duration1 != duration2) == (time1_ns != time2_ns)
    assert (duration1 < duration2) == (time1_ns < time2_ns)
    assert (duration1 > duration2) == (time1_ns > time2_ns)
    assert (duration1 <= duration2) == (time1_ns <= time2_ns)
    assert (duration1 >= duration2) == (time1_ns >= time2_ns)


@example(duration_s=0.0)
@example(duration_s=1.0)
@example(duration_s=100.0)
@example(duration_s=-100.0)
@given(duration_s=st.floats(min_value=-1e6, max_value=1e6, allow_nan=False, allow_infinity=False))
def test_duration_operations(duration_s: float) -> None:
    """Test Duration-specific operations."""
    duration = Duration(seconds=duration_s)
    abstol = 1e-8
    assert duration.to_s() == pytest.approx(duration_s, abs=abstol)
    assert abs(duration).to_s() == pytest.approx(abs(duration_s), abs=abstol)
    assert (-duration).to_s() == pytest.approx(-duration_s, abs=abstol)

    # Multiplication
    assert (duration * 2).to_s() == pytest.approx(2 * duration_s, abs=abstol)
    assert (duration * -1).to_s() == pytest.approx(-duration_s, abs=abstol)

    # Division by 2
    assert (duration / 2).to_s() == pytest.approx(duration_s / 2, abs=abstol)
    assert (duration / 2.0).to_s() == pytest.approx(duration_s / 2.0, abs=abstol)


@example(nanoseconds=0)
@example(nanoseconds=int(1e9))
@given(nanoseconds=st.integers(min_value=-(2**63), max_value=(2**63 - 1)))
def test_str_and_repr(nanoseconds: int) -> None:
    """Test string representations."""
    sync_time = SyncTime(nanoseconds=nanoseconds)
    duration = Duration(nanoseconds=nanoseconds)

    assert f"SyncTime({nanoseconds}ns)" == repr(sync_time)
    assert f"Duration({nanoseconds}ns)" == repr(duration)
    assert f"SyncTime({nanoseconds}ns)" == str(sync_time)
    assert f"Duration({nanoseconds}ns)" == str(duration)


@example(seconds_s=0.0)
@example(seconds_s=1.0)
@example(seconds_s=100.0)
@given(seconds_s=st.floats(min_value=-1e6, max_value=1e6, allow_nan=False, allow_infinity=False))
def test_equality_with_other_types(seconds_s: float) -> None:
    """Test equality with non-time objects."""
    time = SyncTime(seconds=seconds_s)
    dur = Duration(seconds=seconds_s)

    assert time != "not a time"
    assert time != seconds_s
    assert time is not None
    assert dur != "not a duration"
    assert dur != seconds_s


@example(nanoseconds=0, scale=1)
@example(nanoseconds=int(1e9), scale=2)
@given(
    nanoseconds=st.integers(min_value=-int(1e10), max_value=int(1e10)),
    scale=st.integers(min_value=1, max_value=1000),
)
def test_duration_scaling(nanoseconds: int, scale: int) -> None:
    """Test duration scaling operations."""
    dur = Duration(nanoseconds=nanoseconds)

    # Multiplication
    scaled = dur * scale
    assert scaled.to_ns() == nanoseconds * scale

    # Division
    divided = scaled / scale
    assert abs(divided.to_ns() - nanoseconds) <= 1  # Allow for rounding errors


@example(nanoseconds=0)
@example(nanoseconds=int(1e9))
@given(nanoseconds=st.integers(min_value=-(2**63) + 1, max_value=2**63 - 2))
def test_ordering_consistency(nanoseconds: int) -> None:
    """Test that ordering is consistent with nanosecond values."""
    time = SyncTime(nanoseconds=nanoseconds)
    time_plus = SyncTime(nanoseconds=nanoseconds + 1)
    time_minus = SyncTime(nanoseconds=nanoseconds - 1)
    assert time_minus < time < time_plus


@example(nanoseconds=0)
@example(nanoseconds=1)
@example(nanoseconds=1e9)
@given(nanoseconds=st.integers(min_value=-(2**63), max_value=(2**63 - 1)))
def test_serialization(nanoseconds: int) -> None:
    """Test serialization and deserialization using pickle."""
    sync_time = SyncTime(nanoseconds=nanoseconds)
    serialized_sync_time = pickle.dumps(sync_time)
    deserialized_sync_time = pickle.loads(serialized_sync_time)  # noqa: S301, not a security issue here
    assert deserialized_sync_time == sync_time

    duration = Duration(nanoseconds=nanoseconds)
    serialized_duration = pickle.dumps(duration)
    deserialized_duration = pickle.loads(serialized_duration)  # noqa: S301, not a security issue here
    assert deserialized_duration == duration
