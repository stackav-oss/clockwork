// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/tools/channel_spy/tests/support/test_support.hh"

#include "clockwork/tools/channel_spy/channel_spy.hh"
#include "clockwork/tools/channel_spy/channel_spy_config.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <fmt10/format.h>

#include <fcntl.h>
#include <memory_resource>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace clockwork::tools::tests::support
{

void write_channel_spy_config_file(
  std::string_view test_shm_dir, std::string_view socket_ns, const ChannelSpyConfigTap& config)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const auto config_path = jewels::filesystem::Path{
    fmt::format(ChannelSpy::namespace_channel_spy_config_path_format, test_shm_dir, socket_ns), memory_resource};
  jewels::filesystem::Filesystem filesys(memory_resource);
  if (!filesys.create_directories(config_path.parent_path()))
  {
    throw std::runtime_error("Failed to make create shared memory directory");
  }
  auto open_result = filesys.open(config_path, O_CREAT | O_WRONLY | O_EXCL);
  if (!open_result)
  {
    throw std::runtime_error("Failed to open configuration file");
  }
  if (!filesys.write(open_result.value(), std::as_bytes(std::span{&config, 1U})))
  {
    throw std::runtime_error("Failed to write configuration file");
  }
  if (!open_result->close())
  {
    throw std::runtime_error("Failed to close configuration file");
  }
}

} // namespace clockwork::tools::tests::support
