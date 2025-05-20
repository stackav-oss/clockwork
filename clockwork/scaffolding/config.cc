// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/config.hh"

#include "clockwork/common/process_description.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/utility/fix_clockwork_path.hh"
#include "jewels/uuid/uuid.hh"

#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork::scaffolding
{

jewels::expected<void, jewels::MonoError> setup_configs(
  std::span<const common::ConfigInstanceDescriptionTap> descs,
  jewels::memory::MemoryResource memres_sys,
  jewels::memory::MemoryResource memres_cfg,
  AbstractCasing& casing)
{
  for (const auto& desc : descs)
  {
    auto file = jewels::filesystem::File::open(jewels::fix_clockwork_path(desc.get_config_file_path()));
    if (!file)
    {
      jewels::log_cerr_error(
        "error opening config '{}': {}\nMake sure the file path is correct and it is in the deps list of the bazel exe "
        "target.",
        desc.get_instance_path_name(),
        file.error());
      return jewels::unexpected(jewels::MonoError());
    }
    auto data = file->read_all(memres_sys);
    if (!data)
    {
      jewels::log_cerr_error(
        "error reading config '{}': reading file gave {}", desc.get_instance_path_name(), data.error());
      return jewels::unexpected(jewels::MonoError());
    }
    if (auto result =
          casing.try_instantiate_config(desc.get_config_instance_id(), desc.get_representation_id(), *data, memres_cfg);
        !result)
    {
      jewels::log_cerr_error(
        "error creating config '{}': {}\nTypically this is because you have not declared the config representation in "
        "the casing `representation Protobuf<>`. Note, you can also just delcare your box directly in the casing and "
        "it will automatically declare everything in your box.",
        desc.get_instance_path_name(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

[[nodiscard]] jewels::expected<void, jewels::MonoError>
connect_configs(std::span<const common::ConfigConnectionTap> connections, AbstractCasing& casing)
{
  for (const auto& connection : connections)
  {
    auto result = casing.try_connect_config(connection.get_endpoint_id(), connection.get_config_id());
    if (!result)
    {
      jewels::log_cerr_error(
        "try_connect_config {} -> {} failed: {}",
        connection.get_config_id(),
        connection.get_endpoint_id(),
        result.error());
      return jewels::unexpected(jewels::MonoError());
    }
  }
  return {};
}

} // namespace clockwork::scaffolding
