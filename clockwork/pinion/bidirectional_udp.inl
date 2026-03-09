// IWYU pragma: private, include "clockwork/pinion/bidirectional_udp.hh"
#pragma once

#include "clockwork/pinion/bidirectional_udp.hh"

#include "clockwork/common/process_description_clk_cc.hh" // IWYU pragma: keep
#include "clockwork/io/network_var_packet_clk_cc.hh"      // IWYU pragma: keep
#include "clockwork/io/var_packet_clk_cc.hh"
#include "clockwork/pinion/incoming_udp.hh"
#include "clockwork/pinion/io_connection.hh"
#include "clockwork/pinion/outgoing_udp.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/sock_opt.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/networking/socket_endpoint.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <cerrno>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork::pinion
{

template <class Schema>
// NOLINTNEXTLINE(readability-function-size) TODO(OI-3671)
BidirectionalUdp<Tachyon<Schema>>::BidirectionalUdp(
  jewels::Uuid<common::EndpointClassId> publisher_id,
  jewels::Uuid<common::EndpointClassId> subscriber_id,
  jewels::filesystem::FileDescriptor&& file_descriptor,
  jewels::networking::SocketEndpoint local,
  jewels::networking::SocketEndpoint remote,
  jewels::networking::SocketAddress remote_address,
  std::pmr::vector<::iovec>&& io_vecs,
  std::pmr::vector<::mmsghdr>&& mmsg_hdrs,
  std::pmr::vector<::sockaddr_in>&& msg_names,
  jewels::memory::pmr_unique_ptr<Msg>&& holding_buffer)
  : IncomingUdpImpl<
      Tachyon<Schema>>{publisher_id, std::move(io_vecs), std::move(mmsg_hdrs), std::move(msg_names), std::move(local)},
    OutgoingUdpImpl<Tachyon<Schema>>{subscriber_id, std::move(remote), remote_address, std::move(holding_buffer)},
    file_descriptor_{std::move(file_descriptor)}
{
}

template <class Schema>
template <jewels::networking::SockOption... options>
jewels::expected<jewels::memory::NonNullSharedPtr<BidirectionalUdp<Tachyon<Schema>>>, jewels::filesystem::ErrorCode>
BidirectionalUdp<Tachyon<Schema>>::try_make(
  jewels::memory::MemoryResource memres,
  jewels::Uuid<common::EndpointClassId> publisher_id,
  jewels::Uuid<common::EndpointClassId> subscriber_id,
  jewels::networking::SocketEndpoint local,
  jewels::networking::SocketEndpoint remote,
  const SockOptionValue<options>&... sock_option_values)
{
  const auto local_addr = jewels::networking::SocketAddress::create(std::string{local.host}, local.port);
  if (!local_addr)
  {
    return jewels::unexpected{local_addr.error()};
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

  if (::bind(*file_descriptor, local_addr->ptr(), jewels::networking::SocketAddress::byte_size()) != 0)
  {
    return jewels::unexpected{jewels::filesystem::make_error_code(errno)};
  }

  const auto remote_addr = jewels::networking::SocketAddress::create(std::string{remote.host}, remote.port);
  if (!remote_addr)
  {
    return jewels::unexpected{remote_addr.error()};
  }
  auto holding_buffer = jewels::memory::make_pmr_unique<Msg>(memres);

  return jewels::memory::NonNullSharedPtr<BidirectionalUdp<Msg>>{jewels::memory::make_pmr_shared<BidirectionalUdp<Msg>>(
    memres,
    BidirectionalUdp<Msg>{
      publisher_id,
      subscriber_id,
      std::move(file_descriptor),
      std::move(local),
      std::move(remote),
      *remote_addr,
      std::pmr::vector<::iovec>{1UL, memres},
      std::pmr::vector<::mmsghdr>{1UL, memres},
      std::pmr::vector<::sockaddr_in>{1UL, memres},
      std::move(holding_buffer)})};
}

template <class Schema>
void BidirectionalUdp<Tachyon<Schema>>::notify(AbstractEPollManager& /*epoll*/, int /*efd*/, uint32_t /*events*/)
{
  read();
}

template <class Schema>
void BidirectionalUdp<Tachyon<Schema>>::read()
{
  this->read_impl(fd());
}

template <class Schema>
[[nodiscard]] int BidirectionalUdp<Tachyon<Schema>>::fd() const
{
  return *file_descriptor_;
}

template <class Schema>
[[nodiscard]] jewels::expected<void, jewels::MonoError>
BidirectionalUdp<Tachyon<Schema>>::register_with(AbstractEPollManager& manager)
{
  return this->register_with_impl(manager, fd(), this->shared_from_this());
}

template <class Schema>
[[nodiscard]] jewels::expected<void, IoConnection::Error> BidirectionalUdp<Tachyon<Schema>>::connect_publisher(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::PublisherHandle publisher)
{
  return this->connect_publisher_impl(endpoint_id, std::move(publisher));
}

template <class Schema>
void BidirectionalUdp<Tachyon<Schema>>::notify(const Event& /*event*/)
{
  write();
}

template <class Schema>
void BidirectionalUdp<Tachyon<Schema>>::write()
{
  this->write_impl(fd());
}

template <class Schema>
[[nodiscard]] jewels::expected<jewels::memory::NonNullSharedPtr<pinion::Observer>, IoConnection::Error>
BidirectionalUdp<Tachyon<Schema>>::connect_subscriber(
  jewels::Uuid<common::EndpointClassId> endpoint_id, pinion::SubscriberHandle subscriber)
{
  if (auto res = this->connect_subscriber_impl(endpoint_id, subscriber); !res)
  {
    return jewels::unexpected{res.error()};
  }
  return {jewels::memory::NonNullSharedPtr<pinion::Observer>{
    std::dynamic_pointer_cast<pinion::Observer>(this->shared_from_this())}};
}

} // namespace clockwork::pinion
