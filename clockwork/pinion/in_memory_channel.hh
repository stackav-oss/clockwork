// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"

#include <array>
#include <cstddef>
#include <cstdlib> // IWYU pragma: keep

namespace clockwork
{

///
/// Throw if return is unexpected
///
template <class T, class... Args>
auto unwrap_try_make(Args&&... args);

///
/// Helper class to make in memory channels easier to instantiate.
///
template <typename MsgType, size_t num_slots, bool is_published_once>
class InMemoryChannel : public pinion::AbstractPublisher // NOLINT(fuchsia-multiple-inheritance) spurious
{
public:
  /// @param resource Memory resource for internal allocations.
  /// @param max_msgs_per_exec Maximum messages that may be published per execution cycle.
  ///        Stored in the buffer layout and readable via layout().max_msgs_per_exec.
  ///        Defaults to 1 (legacy single-message behavior).
  explicit InMemoryChannel(jewels::memory::MemoryResource resource, size_t max_msgs_per_exec = 1U);
  ~InMemoryChannel() override = default;
  InMemoryChannel(const InMemoryChannel&) = delete;
  InMemoryChannel& operator=(const InMemoryChannel&) = delete;
  InMemoryChannel(InMemoryChannel&&) = default;
  InMemoryChannel& operator=(InMemoryChannel&&) = default;

  [[nodiscard]] bool handshake() noexcept override;

  [[nodiscard]] pinion::PublisherHandle make_publisher(size_t num_observers);
  [[nodiscard]] pinion::SubscriberHandle make_subscriber();
  [[nodiscard]] pinion::Buffer& buffer();
  [[nodiscard]] const pinion::Buffer& buffer() const;

  pinion::PublisherHandle& publisher() override;
  jewels::expected<pinion::PublisherHandle, jewels::MonoError> extract_publisher() noexcept override;
  [[nodiscard]] size_t get_publish_count() const noexcept override;
  [[nodiscard]] bool on_connect_pending() override;
  [[nodiscard]] size_t num_clients() const noexcept override;
  [[nodiscard]] jewels::expected<pinion::PublisherReservation, pinion::ReserveError>
  reserve(size_t count, bool connected) noexcept override;

  void notify(AbstractEPollManager& epoll, int efd, uint32_t events) override;
  const pinion::BufferLayout& layout() const noexcept override;
  std::ranges::subrange<pinion::SlotRef> available() const override;
  bool add_observer(jewels::memory::ObjectPtr<pinion::Observer> observer) noexcept override;
  int socket() const noexcept override;
  const std::pmr::string& scope() const noexcept override;
  const std::pmr::string& identifier() const noexcept override;
  const std::pmr::string& name() const noexcept override;

private:
  static constexpr pinion::BufferLayout buffer_layout{
    .num_slots = num_slots,
    .message_size = sizeof(MsgType),
    .is_published_once = is_published_once,
  };

  struct Storage
  {
    alignas(pinion::Slot::slot_alignment) std::array<std::byte, buffer_size(buffer_layout)> bytes;
  };

  void notify(const Observer::Event& event) override;

  jewels::memory::pmr_unique_ptr<Storage> storage_;
  pinion::Buffer buffer_;
  jewels::memory::MemoryResource resource_;
  std::pmr::string scope_{"InMemoryChannel:scope"};
  std::pmr::string identifier_{"InMemoryChannel:identifier"};
  std::pmr::string name_{"InMemoryChannel:name"};
  std::pmr::vector<jewels::memory::ObjectPtr<Observer>> observers_;
  std::optional<pinion::PublisherHandle> publisher_;
};

} // namespace clockwork

#include "clockwork/pinion/in_memory_channel.inl"
