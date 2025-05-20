// IWYU pragma: private, include "clockwork/logging/readers/log_processor.hh"
#pragma once

#include "clockwork/logging/readers/log_processor.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "jewels/log_cerr/log_cerr.hh"

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging
{

namespace detail
{

/// Log processor callback implementation for callback with message
/// @tparam T Message type
/// @param[in] callback Message callback
/// @param[in] topic Log topic
/// @param[in] error_behavior Behavior for deserialization failures
/// @param[in] msg Log message
/// @param[in,out] failed_topics Set of topics that have failed due to deserialization errors
template <typename T>
void tappy_callback_wrapper(
  const std::function<void(const T&)>& callback,
  std::string_view topic,
  DeserializationErrorBehavior error_behavior,
  const LoggedMessage& msg,
  std::unordered_set<std::string>& failed_topics)
{
  const std::string topic_str{topic};
  // If this topic has failed deserialization before do not try deserializing more messages from that topic
  if (failed_topics.contains(topic_str))
  {
    return;
  }
  std::unique_ptr<T> deserialized_msg;
  try
  {
    deserialized_msg = std::make_unique<T>();
    deserialize_tachyon<T>(*deserialized_msg, msg);
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("{}: {}", topic, exc.what());
    if (error_behavior == DeserializationErrorBehavior::keep_going)
    {
      failed_topics.emplace(topic_str);
      return;
    }
    throw;
  }
  callback(*deserialized_msg);
}

/// Log processor callback implementation for callback with message and timestamp
/// @tparam T Message type
/// @param[in] callback Message callback
/// @param[in] topic Log topic
/// @param[in] error_behavior Behavior for deserialization failures
/// @param[in] msg Log message
/// @param[in,out] failed_topics Set of topics that have failed due to deserialization errors
template <typename T>
void tappy_callback_wrapper(
  const std::function<void(const LogTimestamp&, const T&)>& callback,
  std::string_view topic,
  DeserializationErrorBehavior error_behavior,
  const LoggedMessage& msg,
  std::unordered_set<std::string>& failed_topics)
{
  const std::string topic_str{topic};
  // If this topic has failed deserialization before do not try deserializing more messages from that topic
  if (failed_topics.contains(topic_str))
  {
    return;
  }
  std::unique_ptr<T> deserialized_msg;
  try
  {
    deserialized_msg = std::make_unique<T>();
    deserialize_tachyon<T>(*deserialized_msg, msg);
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("{}: {}", topic, exc.what());
    if (error_behavior == DeserializationErrorBehavior::keep_going)
    {
      failed_topics.emplace(topic_str);
      return;
    }
    throw;
  }
  callback(msg.publish_time, *deserialized_msg);
}

} // namespace detail

template <typename T>
LogProcessor& LogProcessor::add_tappy_callback(
  std::string_view topic, std::function<void(const T&)> callback, DeserializationErrorBehavior error_behavior)
  requires clockwork::TappyType<T>
{
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
  auto wrapped =
    [this, callback = std::move(callback), topic = std::string{topic}, error_behavior](const LoggedMessage& msg)
  { detail::tappy_callback_wrapper(callback, topic, error_behavior, msg, failed_topics_); };

  return add_raw_msg_callback(topic, std::move(wrapped));
}

template <typename T>
LogProcessor& LogProcessor::add_tappy_callback(
  std::string_view topic,
  std::function<void(const LogTimestamp& publish_time, const T&)> callback,
  DeserializationErrorBehavior error_behavior)
  requires clockwork::TappyType<T>
{
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
  auto wrapped =
    [this, callback = std::move(callback), topic = std::string{topic}, error_behavior](const LoggedMessage& msg)
  { detail::tappy_callback_wrapper(callback, topic, error_behavior, msg, failed_topics_); };

  return add_raw_msg_callback(topic, std::move(wrapped));
}

template <typename T>
std::function<bool(std::string_view)>
LogProcessor::augment_topic_filter(std::function<bool(std::string_view)> filter, const T& topics)
{
  auto cb_topics = std::vector<std::string>(topics.begin(), topics.end());
  return [filter = std::move(filter), cb_topics = std::move(cb_topics)](auto topic)
  {
    // If the topic isnt in the topic list then filter it.
    if (std::find(cb_topics.begin(), cb_topics.end(), topic) == cb_topics.end())
    {
      return false;
    }

    // Check the original filter.
    return filter ? filter(topic) : true;
  };
}

} // namespace clockwork_logging
