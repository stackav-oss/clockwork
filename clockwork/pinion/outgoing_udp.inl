// IWYU pragma: private, include "clockwork/pinion/outgoing_udp.hh"
#pragma once

#include "clockwork/pinion/outgoing_udp.hh"

#include "clockwork/io/var_packet.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/detail/socket_payload.hh"
#include "clockwork/pinion/error.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/slot.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/networking/socket_endpoint.hh"
#include "jewels/std/expected.hh"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt10/format.h> // IWYU pragma: keep

#include <cerrno>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <tuple>
#include <utility>

namespace clockwork::pinion
{

template <class Schema>
OutgoingUdp<Tachyon<Schema>>::OutgoingUdp(
  jewels::filesystem::FileDescriptor&& file_descriptor,
  jewels::networking::SocketEndpoint socket_endpoint,
  jewels::networking::SocketAddress address,
  jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer)
  : OutgoingUdpImpl<Tachyon<Schema>>{std::move(socket_endpoint), address, std::move(holding_buffer)},
    file_descriptor_{std::move(file_descriptor)}
{
}

template <class Schema>
template <jewels::networking::SockOption... options>
jewels::expected<jewels::memory::NonNullSharedPtr<OutgoingUdp<Tachyon<Schema>>>, jewels::filesystem::ErrorCode>
OutgoingUdp<Tachyon<Schema>>::try_make(
  jewels::memory::MemoryResource memres,
  jewels::networking::SocketEndpoint socket_endpoint,
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

  auto holding_buffer = jewels::memory::make_pmr_unique<Msg>(memres);
  return jewels::memory::NonNullSharedPtr<OutgoingUdp<Msg>>{jewels::memory::make_pmr_shared<OutgoingUdp<Msg>>(
    memres,
    OutgoingUdp<Msg>{std::move(file_descriptor), std::move(socket_endpoint), *addr, std::move(holding_buffer)})};
}

template <class Schema>
void OutgoingUdp<Tachyon<Schema>>::notify(const Event& /*event*/)
{
  write();
}

template <class Schema>
void OutgoingUdp<Tachyon<Schema>>::write()
{
  this->write_impl(fd());
}

template <class Schema>
jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, IoConnection::Error>
OutgoingUdp<Tachyon<Schema>>::connect_subscriber(pinion::SubscriberHandle subscriber)
{
  if (auto res = this->connect_subscriber_impl(subscriber); !res)
  {
    return jewels::unexpected{res.error()};
  }
  return {jewels::memory::NonNullSharedPtr<pinion::Observer>{this->shared_from_this()}};
}

template <class Schema>
int OutgoingUdp<Tachyon<Schema>>::fd() const
{
  return *file_descriptor_;
}

template <class Schema>
void OutgoingUdpImpl<Tachyon<Schema>>::write_impl(int socket_fd)
{
  if (!subscriber_)
  {
    throw std::runtime_error{
      fmt::format("Subscriber has not been registered for outgoing socket({})", socket_endpoint_)};
  }
  const auto available = subscriber_->available();
  const auto newly_available = pinion::available_starting_from(subscriber_->available(), next_to_consume_);
  if (newly_available)
  {
    write(*subscriber_, *newly_available, socket_fd);
    return;
  }
  if (newly_available.error() == ProgressError::fell_behind)
  {
    const auto num_dropped = std::distance(next_to_consume_, std::begin(available));
    // TODO(OI-2066): Convert to diagnostics
    jewels::log_cerr_error(
      "outgoing socket({}) has fallen behind.  Dropping {} messages.", socket_endpoint_, num_dropped);
    write(*subscriber_, available, socket_fd);
    return;
  }
  // This should only happen if the subscriber state is corrupted which likely means we have some UB.
  throw std::runtime_error{fmt::format(
    "Failed to get available messages from subscriber for outgoing socket({}): {}",
    socket_endpoint_,
    newly_available.error())};
}

template <class Schema>
void OutgoingUdpImpl<Tachyon<Schema>>::write(
  pinion::SubscriberHandle& subscriber, std::ranges::subrange<BufferIterator> available, int socket_fd)
{
  for (next_to_consume_ = std::begin(available); next_to_consume_ != std::end(available); ++next_to_consume_)
  {
    // No need to check slot message size here.  That's already
    // checked when connecting the subscriber.

    // Intentionally copy here so we can guarantee a corrupted packet
    // is never sent to the destination.
    *holding_buffer_ = MessageCast<const Msg>{}(*next_to_consume_);
    if (!subscriber.still_available(next_to_consume_))
    {
      // TODO(OI-2066): Convert to diagnostics
      jewels::log_cerr_error("outgoing socket({}) has fallen behind.  Dropping message.", socket_endpoint_);
      continue;
    }

    auto bytes = detail::as_byte_span(*holding_buffer_);

    ::iovec single_io_vec{
      .iov_base = static_cast<void*>(bytes.data()),
      .iov_len = bytes.size(),
    };

    const ::msghdr msg{
      .msg_name = static_cast<void*>(address_.mutable_ptr()),
      .msg_namelen = jewels::networking::SocketAddress::byte_size(),
      .msg_iov = &single_io_vec,
      .msg_iovlen = 1UL,
      .msg_control = nullptr,
      .msg_controllen = 0UL,
      .msg_flags = 0,
    };

    const auto bytes_sent = ::sendmsg(socket_fd, &msg, 0);
    if (bytes_sent < 0)
    {
      if (errno == EAGAIN || errno == ENOBUFS)
      {
        // TODO(OI-2066): Convert to diagnostics
        jewels::log_cerr_error(
          "Socket is not writable for outgoing socket({}): {}",
          socket_endpoint_,
          jewels::filesystem::make_error_code(errno));
        return;
      }
      // TODO(OI-2066): Convert to diagnostics
      throw std::runtime_error{fmt::format(
        "Failed to send UDP packet for outgoing socket({}): {}",
        socket_endpoint_,
        jewels::filesystem::make_error_code(errno))};
    }

    if (static_cast<size_t>(bytes_sent) != bytes.size())
    {
      throw std::runtime_error{fmt::format(
        "Failed to send UDP packet for outgoing socket({}): {}",
        socket_endpoint_,
        jewels::filesystem::make_error_code(errno))};
    }
  }
}

template <class Schema>
jewels::expected<void, IoConnection::Error>
OutgoingUdpImpl<Tachyon<Schema>>::connect_subscriber_impl(pinion::SubscriberHandle& subscriber)
{
  if (subscriber_)
  {
    return jewels::unexpected{IoConnection::Error::already_connected};
  }
  if (subscriber.layout().message_size != sizeof(Msg))
  {
    return jewels::unexpected{IoConnection::Error::invalid_buffer_layout};
  }
  subscriber_.emplace(std::move(subscriber));
  return {};
}

template <class Schema>
OutgoingUdpImpl<Tachyon<Schema>>::OutgoingUdpImpl(
  jewels::networking::SocketEndpoint socket_endpoint,
  jewels::networking::SocketAddress address,
  jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer)
  : socket_endpoint_{std::move(socket_endpoint)}, address_{address}, holding_buffer_{std::move(holding_buffer)}
{
}

} // namespace clockwork::pinion
