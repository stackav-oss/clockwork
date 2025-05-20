// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/exec_tools.hh"
#include "clockwork/common/forward.hh"
#include "clockwork/common/process_description.hh"
#include "clockwork/common/tests/support/fake_cog.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/tests/support/tmp_shm_namespace.hh"
#include "clockwork/scaffolding/abstract_casing.hh"
#include "clockwork/scaffolding/scaffolding.hh"
#include "clockwork/scaffolding/tests/support/mock_casing.hh"
#include "jewels/cli/tests/support/simple_exit_condition.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"
#include "jewels/testing/tmp_directory_guard.hh"
#include "jewels/uuid/uuid.hh"

#include <catch2/catch_test_macros.hpp>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <span>
#include <thread>
#include <unistd.h>

namespace clockwork::testing
{

template <typename T>
void write_schema(const std::filesystem::path& path, const Tappy<T>& data)
{
  const auto bytes = std::as_bytes(jewels::as_single_item_span(data));
  const jewels::filesystem::File file{path.native(), O_CREAT | O_WRONLY};
  REQUIRE(::write(file.descriptor(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
}

inline void write_schema(const std::filesystem::path& path, const google::protobuf::Message& data)
{
  std::ofstream file{path};
  google::protobuf::io::OstreamOutputStream file_output(&file);
  REQUIRE(google::protobuf::TextFormat::Print(data, &file_output));
}

/// Makes a ShmSubscriber for the given uuid and Tap<Tach<BufferLayout>>
/// @return the subscriber or nullptr if unsuccessful
template <typename Tag>
std::shared_ptr<pinion::ShmSubscriber> make_snooper(
  pinion::ShmChannelFactory& factory, jewels::Uuid<Tag> chan_id, const common::PinionBufferLayoutTap& buffer_desc)
{
  const pinion::BufferLayout layout{
    .num_slots = buffer_desc.get_num_slots(),
    .message_size = buffer_desc.get_message_size(),
  };
  if (auto open = factory.open_subscriber(chan_id.to_string(), layout, 0); open)
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
            body();
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
