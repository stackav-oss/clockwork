// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/onboard/types.hh"

#include "jewels/container/at.hh"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <span>
#include <sys/types.h>

namespace clockwork_logging::onboard
{

[[nodiscard]] size_t data_spans_size(std::span<const std::span<const std::byte>> data_spans)
{
  return std::accumulate(
    data_spans.begin(), data_spans.end(), size_t{0U}, [](size_t lhs, const auto data) { return lhs + data.size(); });
}

void copy_data_spans(std::span<const std::span<const std::byte>> source_spans, std::span<std::byte> dest)
{
  size_t source_index = 0U;
  size_t source_offset = 0U;
  size_t dest_offset = 0U;
  while (source_index < source_spans.size() && dest_offset < dest.size())
  {
    const auto& source = jewels::at(source_spans, static_cast<ssize_t>(source_index));
    while (source_offset < source.size() && dest_offset < dest.size())
    {
      const auto bytes_to_copy = std::min(source.size() - source_offset, dest.size() - dest_offset);
      std::memcpy(
        &jewels::at(dest, static_cast<ssize_t>(dest_offset)),
        &jewels::at(source, static_cast<ssize_t>(source_offset)),
        bytes_to_copy);
      source_offset += bytes_to_copy;
      dest_offset += bytes_to_copy;
    }
    ++source_index;
    source_offset = 0U;
  }
}

void combine_error_counters(const ReaderErrorCounters& counters, ReaderErrorCounters& total_counters)
{
  total_counters.open_failures += counters.open_failures;
  total_counters.invalid_log_headers += counters.invalid_log_headers;
  total_counters.advance_errors += counters.advance_errors;
  total_counters.invalid_records += counters.invalid_records;
  total_counters.missing_channel_metadata += counters.missing_channel_metadata;
  total_counters.missing_schema_metadata += counters.missing_schema_metadata;
}

} // namespace clockwork_logging::onboard
