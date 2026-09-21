// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/otel/otel.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/exporter.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/provider.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/tracer_provider.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace jewels::otel
{

namespace
{

/// Open telemetry endpoint environment variable
constexpr auto otel_exporter_otel_endpoint_env = "OTEL_EXPORTER_OTLP_ENDPOINT";

/// Open telemetry certificate environment variable
constexpr auto otel_exporter_otlp_certificate_env = "OTEL_EXPORTER_OTLP_CERTIFICATE";

/// Trace timeout in seconds
constexpr auto trace_timeout = std::chrono::seconds(5);

/// GRPC gzip compression string
constexpr auto grpc_gzip_compression = "gzip";

/// Service name resource string
constexpr auto service_name_resource = "service.name";

/// Get the endpoint to use
/// @param[in] endpoint Endpoint string, may be empty
///
/// Prefers this order
/// 1. endpoint arg, if truthy
/// 2. OTEL_EXPORTER_OTLP_ENDPOINT environment variable if set
/// 3. None
/// @return Endpoint to use or empty string if none
[[nodiscard]] std::string get_endpoint_str(std::string_view endpoint)
{
  if (!endpoint.empty())
  {
    return std::string{endpoint};
  }
  // NOLINTNEXTLINE(concurrency-mt-unsafe) This code only runs at startup
  if (const auto* endpoint_env = std::getenv(otel_exporter_otel_endpoint_env); endpoint_env != nullptr)
  {
    return endpoint_env;
  }
  return {};
}

/// Get the CA certificates file path to use
/// @return CA certificats file path
[[nodiscard]] std::string get_ca_certs_path()
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) This code only runs at startup
  if (const auto* otlp_cert_path = std::getenv(otel_exporter_otlp_certificate_env); otlp_cert_path != nullptr)
  {
    return otlp_cert_path;
  }
  return "";
}

/// Set up a GRPC span exporter
/// @param[in] endpoint GRPC endpoint
/// @return GRPC span exporter or nullptr on failure
[[nodiscard]] std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>
set_up_grpc_span_exporter(std::string_view endpoint)
{
  opentelemetry::exporter::otlp::OtlpGrpcExporterOptions opts;
  opts.endpoint = std::string{endpoint};
  opts.use_ssl_credentials = endpoint.starts_with("grpcs://");
  opts.ssl_credentials_cacert_path = get_ca_certs_path();
  opts.timeout = trace_timeout;
  opts.compression = grpc_gzip_compression;

  return opentelemetry::exporter::otlp::OtlpGrpcExporterFactory::Create(opts);
}

/// Set up a span exporter that writes to stdout
/// @return GRPC span exporter
[[nodiscard]] std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> set_up_stdout_span_exporter()
{
  return opentelemetry::exporter::trace::OStreamSpanExporterFactory::Create();
}

} // namespace

void set_up_trace_provider(std::string_view service_name, std::string_view endpoint)
{
  const auto endpoint_str = get_endpoint_str(endpoint);
  if (endpoint_str.empty())
  {
    return;
  }

  std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter;
  if (endpoint_str == "stdout")
  {
    exporter = set_up_stdout_span_exporter();
  }
  else if (endpoint_str.starts_with("grpc://") || endpoint_str.starts_with("grpcs://"))
  {
    exporter = set_up_grpc_span_exporter(endpoint_str);
  }
  else
  {
    jewels::log_cerr_error("Unsupported trace endpoint: {}", endpoint_str);
    return;
  }

  auto processor = opentelemetry::sdk::trace::SimpleSpanProcessorFactory::Create(std::move(exporter));
  auto provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(
    std::move(processor), opentelemetry::sdk::resource::Resource::Create({{service_name_resource, service_name}}));
  opentelemetry::sdk::trace::Provider::SetTracerProvider(std::move(provider));
}

[[nodiscard]] std::shared_ptr<opentelemetry::trace::Tracer> get_tracer(std::string_view tracer_name)
{
  auto provider = opentelemetry::trace::Provider::GetTracerProvider();
  return provider->GetTracer(tracer_name);
}

} // namespace jewels::otel
