// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/process_description_clk_cc.hh"
#include "clockwork/pinion/abstract_channel.hh"
#include "clockwork/pinion/abstract_channel_factory.hh"
#include "clockwork/pinion/buffer_layout.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/cli/exit_condition.hh"
#include "jewels/cli/tests/support/simple_exit_condition.hh"
#include "jewels/container/compare.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/filesystem/path.hh"
#include "jewels/std/span.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>

#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <span>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace clockwork::testing
{

template <typename T>
void write_schema(const jewels::filesystem::Path& path, const Tappy<T>& data)
{
  const auto bytes = std::as_bytes(jewels::as_single_item_span(data));
  const jewels::filesystem::File file{path, O_CREAT | O_WRONLY};
  REQUIRE(::write(file.descriptor(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
}

inline void write_schema(const jewels::filesystem::Path& path, const google::protobuf::Message& data)
{
  std::ofstream file{path.c_str()};
  google::protobuf::io::OstreamOutputStream file_output(&file);
  REQUIRE(google::protobuf::TextFormat::Print(data, &file_output));
}

/// Makes a ShmSubscriber for the given uuid and Tap<Tach<BufferLayout>>
/// @return the subscriber or nullptr if unsuccessful
template <typename Tag>
std::shared_ptr<pinion::AbstractSubscriber> make_snooper(
  pinion::AbstractChannelFactory& factory,
  jewels::Uuid<Tag> chan_id,
  const Tappy<common::PinionBufferLayout>& buffer_desc)
{
  const pinion::BufferLayout layout{
    .num_slots = buffer_desc.get_num_slots(),
    .message_size = buffer_desc.get_message_size(),
    .is_published_once = buffer_desc.get_is_published_once(),
  };
  if (auto open = factory.open_subscriber(chan_id.to_string(), "snooper", layout, 0); open)
  {
    return *std::move(open);
  }
  return nullptr;
}

///
/// Helper to manage a `scaffolding::run()` call
/// Holds a thread to run alongside the the `run()` and manages exit conditions and the thread's lifetime
///
class RunStopper
{
public:
  ///
  /// Starts a thread with the given function/lambda and loops it until exit is set
  ///
  template <typename F>
  explicit RunStopper(F&& thread_func)
    : thread_(
        [this, body = std::forward<F>(thread_func)]
        {
          while (*this)
          {
            constexpr auto sleep_time = std::chrono::milliseconds(10);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
            body(*this);
          }
        })
  {
  }

  RunStopper(const RunStopper&) = delete;
  RunStopper(RunStopper&&) = delete;
  RunStopper& operator=(const RunStopper&) = delete;
  RunStopper& operator=(RunStopper&&) = delete;

  ~RunStopper()
  {
    set_exit();
    thread_.join();
  }

  /// Returns true if the thread should keep running, false if the thread should exit
  explicit operator bool()
  {
    return !exit_.check();
  }

  /// Sets the normal run exit flag to shutdown `run()` and the abort flag to shutdown the thread
  void set_exit()
  {
    exit_.signal();
  }

  /// Gets the exit future to pass to `run()`
  jewels::cli::ExitCondition& get_condition()
  {
    return exit_;
  }

private:
  jewels::cli::SimpleExitCondition exit_;
  std::thread thread_;
};

} // namespace clockwork::testing
