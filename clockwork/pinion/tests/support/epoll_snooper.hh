// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/common/abstract_epoll_manager.hh"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace clockwork::pinion
{

/// Test Epoll manager
class EPollSnooper : public AbstractEPollManager
{
public:
  EPollSnooper() noexcept = default;
  ~EPollSnooper() noexcept override = default;
  EPollSnooper(const EPollSnooper&) = delete;
  EPollSnooper& operator=(const EPollSnooper&) = delete;
  EPollSnooper(EPollSnooper&&) = default;
  EPollSnooper& operator=(EPollSnooper&&) = default;

  /// @see AbstractEPollMananger::add
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode>
  add(int efd, uint32_t /*events*/, const std::shared_ptr<AbstractEPollCallback>& callback) override;

  /// @see AbstractEPollMananger::modify
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode> modify(int /*efd*/, uint32_t /*events*/) override;

  /// @see AbstractEPollMananger::remove
  void remove(int efd) override;

  /// @see AbstractEPollMananger::wait
  [[nodiscard]] jewels::expected<void, jewels::filesystem::ErrorCode>
    wait(std::chrono::milliseconds /*timeout*/) override;

  /// Remove and return the most recently registered file descriptor.
  /// @return File descriptor or no value if nothing is registered
  std::optional<int> pop_fd();

  /// Trigger the callback of the given descriptor.
  /// @param[in] efd Event file descriptor
  /// @param[in] events Events passed to the callback
  void notify(int efd, uint32_t events);

private:
  /// Stack of file new descriptors.
  std::vector<int> fds_{};

  /// Callbacks handles.
  std::map<int, std::shared_ptr<AbstractEPollCallback>> callbacks_;
};

} // namespace clockwork::pinion
