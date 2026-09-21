// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_interval.hh"
#include "clockwork/logging/realtime_playback/camera_conversion.hh"
#include "clockwork/logging/realtime_playback/converter_request.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace clockwork_logging::realtime_playback
{

/// Summary of one published converter bundle.
struct BundleConversionResult
{
  LogInterval requested_interval;
  std::string output_path;
  size_t artifact_file_count{};
  uint64_t total_artifact_size_bytes{};
};

/// Convert all configured streams into a bundle at the requested output path.
jewels::BinaryOutcome convert_realtime_playback_bundle(
  jewels::Out<BundleConversionResult> result_out,
  const ConverterRequest& request,
  const CameraConverterFunction& camera_converter);

} // namespace clockwork_logging::realtime_playback
