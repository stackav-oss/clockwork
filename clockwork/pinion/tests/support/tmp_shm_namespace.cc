// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"

#include "clockwork/pinion/shm_channel.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"

#include <cstdlib>
#include <memory_resource>
#include <random>
#include <string_view>

namespace clockwork::pinion::support
{

TmpShmNamespace::TmpShmNamespace()
  : namespace_(gen_namespace())
{
}

[[nodiscard]] const std::string& TmpShmNamespace::get_namespace() const noexcept
{
  return namespace_;
}

[[nodiscard]] const jewels::filesystem::Path& TmpShmNamespace::get_full_path() const noexcept
{
  return directory_.get_path();
}

ShmChannelFactory TmpShmNamespace::make_factory() const
{
  return {
    jewels::memory::MemoryResource(std::pmr::get_default_resource()),
    jewels::filesystem::Directory(get_full_path()),
    get_namespace(),
    ShmChannel::ResumeBehavior::no_resume};
}

std::string TmpShmNamespace::gen_namespace()
{
  constexpr std::string_view characters = "0123456789abcdefghijklmnopqrstuvwxyz";
  constexpr size_t length = 32;

  std::random_device random_device;
  std::mt19937 rng(random_device());
  std::uniform_int_distribution<size_t> chars(0, characters.size() - 1);

  std::string temp_name;
  temp_name.reserve(length);
  for (size_t counter = 0; counter < length; counter++)
  {
    temp_name.push_back(characters[chars(rng)]);
  }

  return temp_name;
}
} // namespace clockwork::pinion::support
