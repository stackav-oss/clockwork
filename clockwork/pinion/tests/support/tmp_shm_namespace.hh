// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/shm_channel_factory.hh"
#include "jewels/testing/tmp_directory_guard.hh"

#include <filesystem>
#include <string>

namespace clockwork::pinion::support
{

///
/// Class to create a temporary directory and socket namespace that can be used for isolated testing of ShmChannels. The
/// directory is created via jewels::testing::TmpDirectoryGuard (see associated docs) while the socket namespace is
/// purely random.
///
class TmpShmNamespace
{
public:
  ///
  /// Create a test directory that is deleted when this object goes out of scope
  ///
  explicit TmpShmNamespace();

  ///
  /// Return the "namespace", a random name that can be used for socket isolation
  ///
  [[nodiscard]] const std::string& get_namespace() const noexcept;

  ///
  /// Return the tempdir path
  ///
  [[nodiscard]] const std::filesystem::path& get_full_path() const noexcept;

  ///
  /// Returns a channel factory created with this temp namespace's parameters
  ///
  [[nodiscard]] ShmChannelFactory make_factory() const;

private:
  static std::string gen_namespace();
  jewels::testing::TmpDirectoryGuard directory_;
  std::string namespace_;
};

} // namespace clockwork::pinion::support
