// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/detail/tcp_socket.hh"
#include "clockwork/pinion/detail/unix_socket.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/networking/socket_address.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

namespace clockwork::pinion
{

namespace
{

struct Tag
{
};

template <typename T>
std::pair<jewels::expected<T, std::errc>, jewels::expected<T, std::errc>> make_socket_pair();

template <>
std::pair<jewels::expected<UnixSocket, std::errc>, jewels::expected<UnixSocket, std::errc>> make_socket_pair()
{
  const std::string socket_name = jewels::Uuid<Tag>::random_uuid().to_string();

  auto listener = UnixSocket::create_bind(socket_name);
  std::ignore = listener->listen(1);
  auto client = UnixSocket::create_connect(socket_name);
  return {std::move(listener), std::move(client)};
}

template <>
std::pair<jewels::expected<TcpSocket, std::errc>, jewels::expected<TcpSocket, std::errc>> make_socket_pair()
{
  const auto host = std::string{"127.0.0.1"};
  const auto listener_addr = jewels::networking::SocketAddress::create(host, 9999);
  const auto client_addr = jewels::networking::SocketAddress::create(host, 9998);
  auto listener = TcpSocket::create_listen(*listener_addr, 1);
  auto client = TcpSocket::create_connect(*listener_addr, *client_addr);
  return {std::move(listener), std::move(client)};
}

} // namespace

TEMPLATE_TEST_CASE("Socket | set nonblocking", "", UnixSocket, TcpSocket)
{
  constexpr size_t buffer_size = 4;
  std::mutex mutex;
  std::condition_variable condition;
  bool sleeping = false;
  bool received = false;
  bool exit = false;

  auto sockets = make_socket_pair<TestType>();
  auto& [listener, client] = sockets;
  auto server = jewels::filesystem::FileDescriptor(::accept(listener->descriptor(), nullptr, nullptr));
  REQUIRE(listener);
  REQUIRE(client);
  REQUIRE(server);

  auto write = [&server, &mutex]()
  {
    const std::array<char, buffer_size> sendbuf{};
    const std::scoped_lock lock(mutex);
    CHECK(::send(*server, sendbuf.data(), buffer_size, 0) != -1);
  };
  auto signal = [&mutex, &sleeping, &condition]()
  {
    {
      const std::scoped_lock lock(mutex);
      sleeping = false;
    }
    condition.notify_one();
  };
  auto wait_for_sleeping = [&mutex, &sleeping](uint32_t time_ms)
  {
    constexpr uint32_t step_ms = 100;
    for (uint32_t i = 0;; i += step_ms)
    {
      std::unique_lock lock(mutex);
      if (sleeping || i >= time_ms)
      {
        return sleeping;
      }
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
    }
  };
  auto thread_body = [&mutex, &sleeping, &exit, &condition, &client, &received]()
  {
    while (true)
    {
      {
        std::unique_lock lock(mutex);
        sleeping = true;
        condition.wait(lock, [&] { return !sleeping || exit; });
      }
      if (exit)
      {
        break;
      }
      std::array<char, buffer_size> recvbuf{};
      // Clang-tidy thinks the above mutex is still held when recv is called here
      // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
      const ssize_t result = ::recv(client->descriptor(), recvbuf.data(), recvbuf.size(), 0);
      {
        const std::scoped_lock lock(mutex);
        received = (result != -1);
      }
    }
  };

  std::thread thread(std::move(thread_body));

  CHECK(wait_for_sleeping(1000));

  // Starts in blocking mode
  signal();                       // wake
  CHECK(!wait_for_sleeping(300)); // confirm that it blocks instead of going back to sleep
  write();                        // send data
  CHECK(wait_for_sleeping(500));  // the blocking call should exit
  CHECK(received);                // ... with data

  // Set to non-block
  CHECK(set_nonblocking(client->descriptor(), true));
  signal();                      // wake
  CHECK(wait_for_sleeping(300)); // should go back to sleep
  CHECK(!received);              // ... without data
  write();                       // write data
  signal();                      // wake
  CHECK(wait_for_sleeping(500)); // should go back to sleep
  CHECK(received);               // ... but with the data this time

  // Set back to blocking and retest
  CHECK(set_nonblocking(client->descriptor(), false));
  signal();
  CHECK(!wait_for_sleeping(300));
  write();
  CHECK(wait_for_sleeping(500));
  CHECK(received);

  {
    const std::scoped_lock lock(mutex);
    exit = true;
  }
  condition.notify_one();
  thread.join();
}

} // namespace clockwork::pinion
