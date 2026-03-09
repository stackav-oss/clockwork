// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// Open Telemetry helpers for C++

#include <opentelemetry/trace/tracer.h>

#include <memory>
#include <string_view>

namespace jewels::otel
{

/// Set up the open telemetry trace provider
/// @param[in] service_name Service name
/// @param[in] endpoint Open telemetry service endpoint
void set_up_trace_provider(std::string_view service_name, std::string_view endpoint = {});

/// Get a tracer
/// @param[in] tracer_name Tracer name
[[nodiscard]] std::shared_ptr<opentelemetry::trace::Tracer> get_tracer(std::string_view tracer_name);

} // namespace jewels::otel
