// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/channel_spy.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_init_clk_cc_dial.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt/format.h>

#include <fcntl.h>
#include <span>
#include <stdexcept>
#include <string>

namespace clockwork::tools
{

void execute_cog(SpyConfigInitCogDial& dial)
{
  const auto& spy_config = dial.get_configs().get_spy_config();
  jewels::filesystem::Filesystem filesys{dial.get_resources().get_memres()};
  jewels::filesystem::Path config_path{
    fmt::format(ChannelSpy::default_channel_spy_config_path_format, "/tmp"), dial.get_resources().get_memres()};
  if (const auto mkdir_result = filesys.create_directories(config_path.parent_path_view()); !mkdir_result)
  {
    const auto msg = fmt::format("Failed to create the spy config direcory: {}", mkdir_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  auto open_result = filesys.open(config_path.string_view(), O_CREAT | O_WRONLY | O_TRUNC);
  if (!open_result)
  {
    const auto msg = fmt::format("Failed to create the spy config file: {}", open_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  if (const auto write_result = filesys.write(open_result.value(), std::as_bytes(std::span{&spy_config, 1U}));
      !write_result)
  {
    const auto msg = fmt::format("Failed to write the spy config file: {}", open_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
  if (const auto close_result = open_result.value().close(); !close_result)
  {
    const auto msg = fmt::format("Failed to close the spy config file: {}", close_result.error());
    jewels::log_cerr_error("{}", msg);
    throw std::runtime_error(msg);
  }
}

} // namespace clockwork::tools
