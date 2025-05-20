// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/channel_spy_config.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_init_cog_dial.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt10/format.h>

#include <cerrno>
#include <fcntl.h>
#include <span>
#include <stdexcept>

namespace clockwork::tools
{

namespace
{

constexpr auto channel_spy_config_file_path = "/dev/shm/clockwork/pinion/pub/channel_spy_config.tachyon";

} // namespace

void execute_cog(SpyConfigInitCogDial& dial)
{
  const auto& spy_config = dial.get_configs().get_spy_config();
  jewels::filesystem::Filesystem filesys{dial.get_resources().get_memres()};
  auto open_result = filesys.open(channel_spy_config_file_path, O_CREAT | O_WRONLY | O_EXCL);
  if (!open_result)
  {
    if (open_result.error().value() == EEXIST)
    {
      jewels::log_cerr_info("Not creating the spy config file: {}", open_result.error());
      return;
    }
    const auto msg = fmt::format("Failed to create the spy config file: {}", open_result.error());
    throw std::runtime_error(msg);
  }
  if (const auto write_result = filesys.write(open_result.value(), std::as_bytes(std::span{&spy_config, 1U}));
      !write_result)
  {
    const auto msg = fmt::format("Failed to write the spy config file: {}", open_result.error());
    throw std::runtime_error(msg);
  }
  if (const auto close_result = open_result.value().close(); !close_result)
  {
    const auto msg = fmt::format("Failed to close the spy config file: {}", close_result.error());
    throw std::runtime_error(msg);
  }
}

} // namespace clockwork::tools
