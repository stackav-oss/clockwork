// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/shm_channel_factory.hh"

#include "clockwork/pinion/shm_channel.hh"
#include "clockwork/pinion/shm_publisher.hh"
#include "clockwork/pinion/shm_subscriber.hh"
#include "jewels/container/bounded_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/std/expected.hh"

#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{

ShmChannelFactory::~ShmChannelFactory() = default;

jewels::expected<ShmChannelFactory, jewels::MonoError> ShmChannelFactory::make(
  jewels::memory::MemoryResource memres,
  std::string_view nmsp,
  std::string_view root,
  ShmChannel::ResumeBehavior resume_behavior)
{
  constexpr size_t ns_buf_size = 200;
  using Dir = jewels::filesystem::Directory;

  auto sock_ns = jewels::container::BoundedString<ns_buf_size>::truncated("/clockwork");
  if (!nmsp.empty() && !(sock_ns.try_concat("/") && sock_ns.try_concat(nmsp)))
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  if (!sock_ns.try_concat("/pinion/pub"))
  {
    return jewels::unexpected(jewels::MonoError{});
  }

  auto shm_dir =
    (nmsp.empty() ? Dir::create_open(Dir::at_cwd, root, "clockwork", "pinion", "pub")
                  : Dir::create_open(Dir::at_cwd, root, "clockwork", nmsp, "pinion", "pub"));
  if (!shm_dir)
  {
    jewels::log_cerr_error("Failed to create/open shm channel directory: {}", shm_dir.error());
    return jewels::unexpected(jewels::MonoError{});
  }
  return ShmChannelFactory(std::move(memres), std::move(*shm_dir), sock_ns, resume_behavior);
}

ShmChannelFactory::ShmChannelFactory(
  jewels::memory::MemoryResource memres,
  jewels::filesystem::Directory shm_dir,
  std::string_view socket_ns,
  ShmChannel::ResumeBehavior resume_behavior)
  : memres_(std::move(memres)),
    shm_dir_(std::move(shm_dir)),
    socket_ns_(socket_ns, memres_),
    resume_behavior_(resume_behavior)
{
}

jewels::expected<std::shared_ptr<AbstractPublisher>, ShmChannel::Error> ShmChannelFactory::open_publisher(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return open_shm_publisher(uuid_str, channel_name, layout, max_subscribers);
}

jewels::expected<std::shared_ptr<AbstractSubscriber>, ShmChannel::Error> ShmChannelFactory::open_subscriber(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return open_shm_subscriber(uuid_str, channel_name, layout, max_subscribers);
}

jewels::expected<std::shared_ptr<AbstractSubscriber>, ShmChannel::Error> ShmChannelFactory::open_spy(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return open_shm_spy(uuid_str, channel_name, layout, max_subscribers);
}

jewels::expected<std::shared_ptr<ShmPublisher>, ShmChannel::Error> ShmChannelFactory::open_shm_publisher(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return ShmPublisher::open(
    memres_, shm_dir_, socket_ns_, uuid_str, channel_name, layout, max_subscribers, max_subscribers, resume_behavior_);
}

jewels::expected<std::shared_ptr<ShmSubscriber>, ShmChannel::Error> ShmChannelFactory::open_shm_subscriber(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return to_shared(
    ShmSubscriber::open(
      memres_,
      shm_dir_,
      socket_ns_,
      uuid_str,
      channel_name,
      layout,
      max_subscribers,
      ShmSubscriber::SubscriberRole::subscriber,
      resume_behavior_));
}

jewels::expected<std::shared_ptr<ShmSubscriber>, ShmChannel::Error> ShmChannelFactory::open_shm_spy(
  std::string_view uuid_str, std::string_view channel_name, const BufferLayout& layout, size_t max_subscribers)
{
  return to_shared(
    ShmSubscriber::open(
      memres_,
      shm_dir_,
      socket_ns_,
      uuid_str,
      channel_name,
      layout,
      max_subscribers,
      ShmSubscriber::SubscriberRole::spy,
      resume_behavior_));
}

const std::pmr::string& ShmChannelFactory::socket_ns() const noexcept
{
  return socket_ns_;
}

jewels::filesystem::Directory& ShmChannelFactory::directory() noexcept
{
  return shm_dir_;
}

ShmChannel::ResumeBehavior ShmChannelFactory::resume_behavior() const noexcept
{
  return resume_behavior_;
}

template <typename T>
jewels::expected<std::shared_ptr<T>, pinion::ShmChannel::Error>
ShmChannelFactory::to_shared(jewels::expected<T, pinion::ShmChannel::Error>&& obj)
{
  auto make_shared = [this](T&& value)
  { return jewels::memory::make_pmr_shared<std::decay_t<T>>(memres_, std::move(value)); };
  return std::move(obj).transform(make_shared);
}

} // namespace clockwork::pinion
