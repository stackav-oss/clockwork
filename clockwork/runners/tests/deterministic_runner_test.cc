// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/common/abstract_cog.hh"
#include "clockwork/common/abstract_cog_queue.hh"
#include "clockwork/common/abstract_timer.hh"
#include "clockwork/common/cog_execution_error.hh"
#include "clockwork/pinion/buffer.hh"
#include "clockwork/pinion/in_memory_channel.hh"
#include "clockwork/pinion/observer.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "clockwork/runners/deterministic_cog_queue.hh"
#include "clockwork/runners/deterministic_runner.hh"
#include "clockwork/runners/deterministic_timer.hh"
#include "jewels/cli/exit_condition_signal.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <boost/container/allocator_traits.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace clockwork
{
namespace
{

int64_t to_ms(jewels::time::SyncTime time)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

template <typename T>
T pop(std::deque<T>& queue)
{
  T result = queue.front();
  queue.pop_front();
  return result;
}

struct TimerEvent
{
  jewels::time::SyncTime event_time = {};
  jewels::time::SyncTime actual_time = {};
  bool operator==(const TimerEvent& rhs) const = default;
};

class CheckPublishObserver : public pinion::Observer
{
public:
  void notify(const Event& /*event*/) override
  {
    ++num_published;
  }
  std::size_t num_published = 0;
};
class TestChannelPublisher : public AbstractChannelPublisher
{
public:
  TestChannelPublisher(pinion::PublisherHandle publisher, std::vector<jewels::time::SyncTime> event_times_input)
    : event_times(std::move(event_times_input)), underlying_publisher(std::move(publisher))
  {
  }

  jewels::expected<void, jewels::MonoError> initialize() override
  {
    return {};
  }

  std::optional<jewels::time::SyncTime> try_next_message_time() override
  {
    if (current_event_index < event_times.size())
    {
      return event_times.at(current_event_index);
    }
    return std::nullopt;
  }

  jewels::expected<void, jewels::MonoError> publish_next_message() override
  {
    if (current_event_index >= event_times.size())
    {
      return jewels::unexpected(jewels::MonoError{});
    }
    auto reserve_result = underlying_publisher.reserve();
    REQUIRE(reserve_result);
    auto& reserved_slot = reserve_result.value();
    REQUIRE(reserved_slot.commit(event_times.at(current_event_index)));
    ++current_event_index;
    return {};
  }

  bool messages_remaining() override
  {
    return current_event_index < event_times.size();
  }

  std::vector<jewels::time::SyncTime> event_times;
  std::size_t current_event_index = 0;
  pinion::PublisherHandle underlying_publisher;
};
// We're inheriting from pure interfaces.
// NOLINTNEXTLINE(fuchsia-multiple-inheritance) Test code, inheriting abstract interfaces.
class TestCog : public AbstractCog, public pinion::Observer
{
public:
  explicit TestCog(
    jewels::memory::ObjectPtr<AbstractCogQueue> queue,
    jewels::memory::ObjectPtr<DeterministicTimer> timer,
    std::chrono::milliseconds timer_period,
    jewels::memory::ObjectPtr<pinion::SubscriberHandle> subscriber,
    std::deque<std::pair<int, int64_t>>& events,
    int identifer)
    : AbstractCog(queue),
      timer_period_(timer_period),
      timer_(timer),
      subscriber_(subscriber),
      events_(events),
      id_(identifer)
  {
  }

  [[nodiscard]] std::string_view get_name() const override
  {
    return "clockwork::TestCog";
  }

  jewels::expected<void, jewels::MonoError> prime(jewels::time::SyncTime /*start_time*/) override
  {
    return {};
  }

  void notify(const Event& event) override
  {
    add_to_ready_queue(event.current_time);
  }

  jewels::expected<void, CogExecutionError> prepare_for_execution(jewels::time::SyncTime /*current_time*/) override
  {
    auto reentry_lock = std::unique_lock(reentry_mutex_, std::defer_lock);

    if (!reentry_lock.try_lock())
    {
      return jewels::unexpected(CogExecutionError::reentry_lock_contention);
    }

    reentry_lock_ = std::move(reentry_lock);

    return {};
  }

  jewels::expected<void, CogExecutionError> execute(CogExecuteParams params) override
  {
    events_.emplace_back(id_, to_ms(params.start_time));
    reentry_lock_ = {};
    CHECK(timer_->start(params.start_time + timer_period_, timer_period_));
    return {};
  }

private:
  std::chrono::milliseconds timer_period_;
  jewels::memory::ObjectPtr<DeterministicTimer> timer_;
  std::mutex reentry_mutex_;
  std::unique_lock<std::mutex> reentry_lock_;

  jewels::memory::ObjectPtr<pinion::SubscriberHandle> subscriber_;
  std::deque<std::pair<int, int64_t>>& events_;
  int id_;

  pinion::BufferIterator last_message_;
};

TEST_CASE("execute", "[DeterministicRunner]")
{
  using namespace std::chrono_literals;

  std::deque<std::pair<int, int64_t>> events;

  constexpr int64_t timer1_period_ms = 10;
  constexpr int64_t timer2_period_ms = 11;
  constexpr auto start_time = jewels::time::SyncTime(3000ms);
  constexpr auto end_time = start_time + 300ms;
  constexpr size_t channel_size = 1000;
  const std::vector<jewels::time::SyncTime> publish_events = {start_time + 12ms, start_time + 17ms, start_time + 47ms};
  auto resource = jewels::memory::MemoryResource(std::pmr::new_delete_resource());

  auto queue = std::make_shared<DeterministicCogQueue>(resource);
  auto queue_ptr = jewels::memory::make_non_null_from_ref(*queue);

  auto channel1 = std::make_unique<InMemoryChannel<TimerEvent, channel_size>>(resource);
  auto publisher1 = channel1->make_publisher(1);
  auto subscriber1 = channel1->make_subscriber();

  auto channel2 = std::make_unique<InMemoryChannel<TimerEvent, channel_size>>(resource);
  auto publisher2 = channel2->make_publisher(1);
  auto subscriber2 = channel2->make_subscriber();

  auto channel3 = std::make_unique<InMemoryChannel<TimerEvent, channel_size>>(resource);
  auto publisher3 = channel3->make_publisher(1);
  auto test_publisher_observer = std::make_shared<CheckPublishObserver>();
  REQUIRE(publisher3.add_observer(jewels::memory::make_non_null_from_ref(*test_publisher_observer)));

  auto timer1 = std::make_shared<DeterministicTimer>();
  auto timer2 = std::make_shared<DeterministicTimer>();

  TestCog cog1(
    queue_ptr,
    jewels::memory::make_non_null_from_ref(*timer1),
    std::chrono::milliseconds(timer1_period_ms),
    jewels::memory::make_non_null_from_ref(subscriber1),
    events,
    1);
  auto cog1_ptr = jewels::memory::make_non_null_from_ref(cog1);
  REQUIRE(publisher1.add_observer(cog1_ptr));

  TestCog cog2(
    queue_ptr,
    jewels::memory::make_non_null_from_ref(*timer2),
    std::chrono::milliseconds(timer2_period_ms),
    jewels::memory::make_non_null_from_ref(subscriber2),
    events,
    2);
  auto cog2_ptr = jewels::memory::make_non_null_from_ref(cog2);
  REQUIRE(publisher2.add_observer(cog2_ptr));

  timer1->set_observer(cog1_ptr);
  timer2->set_observer(cog2_ptr);

  std::pmr::vector<std::shared_ptr<AbstractTimer>> timer_vec;
  timer_vec.emplace_back(timer1);
  timer_vec.emplace_back(timer2);

  const std::pmr::unordered_map<jewels::memory::ObjectPtr<AbstractCog>, int16_t> cog_to_gpu_id;

  DeterministicRunner runner(
    DeterministicRunnerConfig{
      .resource = resource,
      .cogs =
        {{
           .cog = cog1_ptr,
         },
         {
           .cog = cog2_ptr,
         }},
      .timers = timer_vec,
      .queue = queue,
      .start_time = start_time,
      .end_time = end_time,
      .channel_publisher = std::make_shared<TestChannelPublisher>(std::move(publisher3), publish_events),
      .cog_to_gpu_id = cog_to_gpu_id});

  CHECK(timer1->start(
    start_time + std::chrono::milliseconds(timer1_period_ms), std::chrono::milliseconds(timer1_period_ms)));
  CHECK(timer2->start(
    start_time + std::chrono::milliseconds(timer2_period_ms), std::chrono::milliseconds(timer2_period_ms)));
  jewels::cli::SignalExitCondition exit;
  SECTION("Regular Run")
  {
    runner.start(start_time, end_time, exit);

    // Note the timers produce their first event at `start + period` so start the counter at `start + 1`
    for (int64_t time_ms = to_ms(start_time) + 1; time_ms <= to_ms(end_time); time_ms++)
    {
      INFO("timestamp " << time_ms);
      if ((time_ms - to_ms(start_time)) % timer2_period_ms == 0)
      {
        auto event = pop(events);
        REQUIRE(event.first == 2);
        REQUIRE(event.second == time_ms);
      }

      if ((time_ms - to_ms(start_time)) % timer1_period_ms == 0)
      {
        auto event = pop(events);
        REQUIRE(event.first == 1);
        REQUIRE(event.second == time_ms);
      }
    }
    REQUIRE(test_publisher_observer->num_published == publish_events.size());
  }
  SECTION("Early termination")
  {
    exit.signal();
    runner.start(start_time, end_time, exit);
    REQUIRE(events.empty());
  }
}

} // namespace
} // namespace clockwork
