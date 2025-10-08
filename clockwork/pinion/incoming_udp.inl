// IWYU pragma: private, include "clockwork/pinion/incoming_udp.hh"
#pragma once

#include "clockwork/pinion/incoming_udp.hh"

#include "clockwork/common/abstract_epoll_manager.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/io/network_var_packet.hh"
#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/networking/socket_endpoint.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/format.h> // IWYU pragma: keep

#include <cerrno>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork::pinion
{
namespace detail
{
template <class Schema>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
read_size_check(size_t bytes_read, const Tachyon<Schema>& /*msg*/)
{
  if (bytes_read != sizeof(Tachyon<Schema>))
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  return {};
}

template <auto packet_size>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
read_size_check(size_t bytes_read, Tachyon<io::VarPacket<packet_size>>& msg)
{
  if (bytes_read > packet_size)
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  msg.bytes.resize(bytes_read);
  return {};
}

template <auto packet_size>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
read_size_check(size_t bytes_read, Tachyon<io::NetworkVarPacket<packet_size>>& msg)
{
  if (bytes_read > packet_size)
  {
    return jewels::unexpected{jewels::MonoError{}};
  }
  msg.bytes.resize(bytes_read);
  return {};
}
} // namespace detail

template <class Schema>
IncomingUdp<Tachyon<Schema>>::IncomingUdp(
  jewels::Uuid<common::EndpointClassId> publisher_id,
  jewels::filesystem::FileDescriptor&& file_descriptor,
  std::pmr::vector<::iovec>&& io_vecs,
  std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
  std::pmr::vector<::sockaddr_in>&& msg_names,
  jewels::networking::SocketEndpoint socket_endpoint)
  : IncomingUdpImpl<Tachyon<
      Schema>>{publisher_id, std::move(io_vecs), std::move(mmsg_hdrs), std::move(msg_names), std::move(socket_endpoint)},
    file_descriptor_{std::move(file_descriptor)}
{
}

template <class Schema>
template <jewels::networking::SockOption... options>
jewels::expected<jewels::memory::NonNullSharedPtr<IncomingUdp<Tachyon<Schema>>>, jewels::filesystem::ErrorCode>
IncomingUdp<Tachyon<Schema>>::try_make(
  jewels::memory::MemoryResource memres,
  jewels::Uuid<common::EndpointClassId> publisher_id,
  jewels::networking::SocketEndpoint socket_endpoint,
  size_t batch_size,
  const SockOptionValue<options>&... sock_option_values)
{
  const auto addr = jewels::networking::SocketAddress::create(std::string{socket_endpoint.host}, socket_endpoint.port);
  if (!addr)
  {
    return jewels::unexpected{addr.error()};
  }
  jewels::filesystem::FileDescriptor file_descriptor{::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)};

  if (!file_descriptor)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }

  if (auto result = handle_sock_options(*file_descriptor, std::make_tuple(sock_option_values...)); !result)
  {
    return jewels::unexpected{result.error()};
  }

  if (::bind(*file_descriptor, addr->ptr(), jewels::networking::SocketAddress::byte_size()) != 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }

  if (batch_size == 0UL)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(EINVAL)};
  }

  return jewels::memory::NonNullSharedPtr<IncomingUdp<Msg>>{jewels::memory::make_pmr_shared<IncomingUdp<Msg>>(
    memres,
    IncomingUdp<Msg>{
      publisher_id,
      std::move(file_descriptor),
      std::pmr::vector<::iovec>{batch_size, memres},
      std::pmr::vector<::mmsghdr>{batch_size, memres},
      std::pmr::vector<::sockaddr_in>{batch_size, memres},
      std::move(socket_endpoint)})};
}

template <class Schema>
void IncomingUdp<Tachyon<Schema>>::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/)
{
  read();
}

template <class Schema>
void IncomingUdp<Tachyon<Schema>>::read()
{
  this->read_impl(fd());
}

template <class Schema>
int IncomingUdp<Tachyon<Schema>>::fd() const
{
  return *file_descriptor_;
}

template <class Schema>
jewels::expected<void, IoConnection::Error> IncomingUdp<Tachyon<Schema>>::connect_publisher(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle publisher)
{
  return this->connect_publisher_impl(endpoint_id, std::move(publisher));
}

template <class Schema>
jewels::expected<void, jewels::MonoError> IncomingUdp<Tachyon<Schema>>::register_with(AbstractEPollManager& manager)
{
  return this->register_with_impl(manager, fd(), this->shared_from_this());
}

template <class Schema>
void IncomingUdpImpl<Tachyon<Schema>>::read_impl(int socket_fd)
{
  if (!publisher_)
  {
    throw std::runtime_error{
      fmt::format("Publisher has not been registered for incoming socket({})", socket_endpoint_)};
  }
  if (!reserved_batch_)
  {
    throw std::runtime_error{
      fmt::format("A batch should always be reserved for incoming socket({})", socket_endpoint_)};
  }

  auto max_size = io_vecs_.size();

  auto mmsg_hdrs_available = std::span{mmsg_hdrs_}.subspan(max_size - slots_available_);
  const auto messages_read =
    ::recvmmsg(socket_fd, mmsg_hdrs_available.data(), static_cast<uint32_t>(mmsg_hdrs_available.size()), 0, nullptr);

  if (messages_read == 0UL)
  {
    // TODO(OI-2066): Convert to diagnostics
    throw std::runtime_error{fmt::format("incoming socket({}) was notified, but nothing to read.", socket_endpoint_)};
  }
  if (messages_read < 0)
  {
    // TODO(OI-2066): Convert to diagnostics
    throw std::runtime_error{fmt::format("incoming socket({}) failed to read.", socket_endpoint_)};
  }

  auto mmsg_hdrs_read = mmsg_hdrs_available.subspan(0, static_cast<size_t>(messages_read));
  auto slots = reserved_batch_->slots();
  for (auto mmsg_index = 0UL; mmsg_index < static_cast<size_t>(messages_read); ++mmsg_index)
  {
    auto& msg_hdr = mmsg_hdrs_read[mmsg_index];
    const auto& msg = msg_hdr.msg_hdr;
    const auto bytes_read = msg_hdr.msg_len;

    if (bytes_read == 0)
    {
      // TODO(OI-2066): Convert to diagnostics
      throw std::runtime_error{fmt::format("Read a payload of size 0 from incoming socket({}).", socket_endpoint_)};
    }

    if ((static_cast<size_t>(msg.msg_flags) & MSG_TRUNC) != 0)
    {
      throw std::runtime_error{fmt::format(
        "Received a payload larger than the capacity of {} for incoming socket({})", sizeof(Msg), socket_endpoint_)};
    }

    auto slot = slots[static_cast<int64_t>(max_size - slots_available_ + mmsg_index)];
    auto& slot_message = *unsafe_marshal_as<Msg>(slot.message());

    if (!detail::read_size_check(static_cast<size_t>(bytes_read), slot_message))
    {
      // This should not happen as UDP does not allow partial writes.
      // TODO(OI-2066): Convert to diagnostics.
      throw std::runtime_error{"Failed to read expected number of bytes"};
    }

    const auto& msg_name = msg_names_.at(max_size - slots_available_ + mmsg_index);
    if (const auto result =
          detail::populate_address_fields(slot_message, msg_name, socket_endpoint_.host, socket_endpoint_.port);
        !result)
    {
      // TODO(OI-2066): Convert to diagnostics
      throw std::runtime_error{
        fmt::format("Error populating the address fields for incoming socket({})", socket_endpoint_)};
    }
  }

  slots_available_ -= static_cast<size_t>(messages_read);
  if (slots_available_ == 0UL)
  {
    /// TODO(OI-2612): Add expiration timer to force publish partial batches.
    if (const auto result = reserved_batch_->commit(jewels::time::SyncClock::now()); !result)
    {
      // TODO(OI-2066): Convert to diagnostics.
      throw std::runtime_error{
        fmt::format("Failed to commit message from IncomignUdp({}) with error: {}", socket_endpoint_, result.error())};
    }
    if (const auto result = reserve_new_batch(*publisher_); !result)
    {
      throw std::runtime_error{
        fmt::format("Failed to reserve new batch after committing for incomign socket({}).", socket_endpoint_)};
    }
    slots_available_ = io_vecs_.size();
  }
}

template <class Schema>
jewels::expected<void, IoConnection::Error> IncomingUdpImpl<Tachyon<Schema>>::connect_publisher_impl(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle&& publisher)
{
  if (publisher_)
  {
    return jewels::unexpected{IoConnection::Error::already_connected};
  }
  if (publisher.layout().message_size != sizeof(Msg))
  {
    return jewels::unexpected{IoConnection::Error::invalid_buffer_layout};
  }
  if (endpoint_id != publisher_id_)
  {
    return jewels::unexpected{IoConnection::Error::unexpected_endpoint_id};
  }
  publisher_.emplace(std::move(publisher));

  if (const auto result = reserve_new_batch(*publisher_); !result)
  {
    jewels::log_cerr_error("Failed to reserve initial batch: {}", result.error());
    return jewels::unexpected{IoConnection::Error::reserve_failure};
  }
  return {};
}

template <class Schema>
jewels::expected<void, ReserveError>
IncomingUdpImpl<Tachyon<Schema>>::reserve_new_batch(pinion::PublisherHandle& publisher_handle)
{
  auto maybe_reserved_batch = publisher_handle.reserve(io_vecs_.size());
  if (!maybe_reserved_batch)
  {
    return jewels::unexpected{maybe_reserved_batch.error()};
  }
  reserved_batch_.emplace(*std::move(maybe_reserved_batch));

  auto slots = reserved_batch_->slots();
  for (auto index = 0UL; index < slots.size(); ++index)
  {
    auto slot = slots[static_cast<int64_t>(index)];
    // The Publishable type isn't used for batch reservations, so we
    // manually default initialize here.
    new (slot.message().data()) Msg{};

    auto& io_vec = io_vecs_.at(index);
    auto bytes = detail::as_writable_byte_span(*unsafe_marshal_as<Msg>(slot.message()));
    io_vec = ::iovec{
      .iov_base = static_cast<void*>(bytes.data()),
      .iov_len = bytes.size(),
    };

    auto& msg_name = msg_names_.at(index);
    mmsg_hdrs_.at(index) = ::mmsghdr{
      .msg_hdr =
        ::msghdr{
          .msg_name = &msg_name,
          .msg_namelen = sizeof(msg_name),
          .msg_iov = &io_vec,
          .msg_iovlen = 1UL,
          .msg_control = nullptr,
          .msg_controllen = 0UL,
          .msg_flags = 0U,
        },
      .msg_len = 0,
    };
  }

  slots_available_ = io_vecs_.size();
  return {};
}

template <class Schema>
jewels::expected<void, jewels::MonoError> IncomingUdpImpl<Tachyon<Schema>>::register_with_impl(
  AbstractEPollManager& manager, int socket_fd, const std::shared_ptr<AbstractEPollCallback>& callback)
{
  const uint32_t events = EPOLLIN;
  if (!manager.add(socket_fd, events, callback))
  {
    jewels::log_cerr_error("internal error: could not add incoming socket({}) to epoll", socket_endpoint_);
    return jewels::unexpected{jewels::MonoError{}};
  }
  return {};
}

template <class Schema>
IncomingUdpImpl<Tachyon<Schema>>::IncomingUdpImpl(
  jewels::Uuid<common::EndpointClassId> publisher_id,
  std::pmr::vector<::iovec>&& io_vecs,
  std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
  std::pmr::vector<::sockaddr_in>&& msg_names,
  jewels::networking::SocketEndpoint socket_endpoint)
  : publisher_id_{publisher_id},
    io_vecs_{std::move(io_vecs)},
    mmsg_hdrs_{std::move(mmsg_hdrs)},
    msg_names_{std::move(msg_names)},
    socket_endpoint_{std::move(socket_endpoint)}
{
}

} // namespace clockwork::pinion
