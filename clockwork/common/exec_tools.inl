// IWYU pragma: private, include "clockwork/common/exec_tools.hh"

#pragma once
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <memory>
#include <span>
#include <string_view>

namespace clockwork
{

template <jewels::meta::ImplicitLifetimeType T>
jewels::expected<T, jewels::MonoError> read_tachyon_config(std::string_view file_path)
{
  auto config_file = jewels::filesystem::File::open(file_path);
  if (!config_file)
  {
    jewels::log_cerr_error("failed to open {}: {}", file_path, config_file.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  T config{};
  auto config_file_read = config_file->pread(std::as_writable_bytes(jewels::as_single_item_span(config)));
  if (!config_file_read)
  {
    jewels::log_cerr_error("failed to read {}: {}", file_path, config_file_read.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  if (config_file_read.value() != sizeof(config))
  {
    jewels::log_cerr_error("failed to read {}: short by {}", file_path, sizeof(config) - config_file_read.value());
    return jewels::unexpected(jewels::MonoError{});
  }
  return config;
}

template <jewels::meta::ImplicitLifetimeType T>
jewels::expected<std::shared_ptr<T>, jewels::MonoError> read_tachyon_config_to_heap(std::string_view file_path)
{
  auto config_file = jewels::filesystem::File::open(file_path);
  if (!config_file)
  {
    jewels::log_cerr_error("failed to open {}: {}", file_path, config_file.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  auto config = std::make_shared<T>();
  auto config_file_read = config_file->pread(std::as_writable_bytes(jewels::as_single_item_span(*config)));
  if (!config_file_read)
  {
    jewels::log_cerr_error("failed to read {}: {}", file_path, config_file_read.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  if (config_file_read.value() != sizeof(*config))
  {
    jewels::log_cerr_error("failed to read {}: short by {}", file_path, sizeof(config) - config_file_read.value());
    return jewels::unexpected(jewels::MonoError{});
  }
  return config;
}
} // namespace clockwork
