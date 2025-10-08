// IWYU pragma: private, include "clockwork/logging/readers/log_processor.hh"
#pragma once

#include "clockwork/logging/readers/log_processor.hh"

#include "clockwork/logging/log_timestamp.hh"
#include "clockwork/logging/readers/serialization.hh"
#include "clockwork/logging/readers/types.hh"
#include "clockwork/repr_iface.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork_logging
{

namespace detail
{

/// Parameters passed to tappy_callback_wrapper
/// @tparam CallbackT Message callback type
template <typename CallbackT>
struct TappyCallbackWrapperParams
{
  /// Constructor
  /// @param[in] callback_in Message callback
  /// @param[in] topic_in Log topic
  /// @param[in] error_behavior_in Error behavior
  /// @param[in] failed_topics_in Set of topics that have failed due to deserialization errors
  /// @param[in] schema_upgrader_in Schema upgrader, this will be nullptr if the schemas are compatible
  TappyCallbackWrapperParams(
    CallbackT callback_in,
    std::string_view topic_in,
    DeserializationErrorBehavior error_behavior_in,
    jewels::memory::ObjectPtr<std::unordered_set<std::string>> failed_topics_in,
    std::unique_ptr<clockwork::serialization::TachyonUpgrader> schema_upgrader_in);

  ~TappyCallbackWrapperParams() noexcept = default;
  TappyCallbackWrapperParams(const TappyCallbackWrapperParams&) = delete;
  TappyCallbackWrapperParams& operator=(const TappyCallbackWrapperParams&) = delete;
  TappyCallbackWrapperParams(TappyCallbackWrapperParams&&) noexcept = default;
  TappyCallbackWrapperParams& operator=(TappyCallbackWrapperParams&&) noexcept = default;

  /// Message callback
  CallbackT callback;
  /// Log topic
  std::string topic;
  /// Error behavior
  DeserializationErrorBehavior error_behavior;
  /// Set of topics that have failed due to deserialization errors
  jewels::memory::ObjectPtr<std::unordered_set<std::string>> failed_topics;
  /// Schema upgrader, this will be nullptr if the schemas are compatible
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> schema_upgrader;
};

template <typename CallbackT>
TappyCallbackWrapperParams<CallbackT>::TappyCallbackWrapperParams(
  CallbackT callback_in,
  std::string_view topic_in,
  DeserializationErrorBehavior error_behavior_in,
  jewels::memory::ObjectPtr<std::unordered_set<std::string>> failed_topics_in,
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> schema_upgrader_in)
  : callback(std::move(callback_in)),
    topic(topic_in),
    error_behavior(error_behavior_in),
    failed_topics(failed_topics_in),
    schema_upgrader(std::move(schema_upgrader_in))
{
}

/// Log processor callback implementation for callback with message
/// @tparam T Message type
/// @param[in] params Tappy callback wrapper parameters
/// @param[in] msg Log message
template <typename T>
void tappy_callback_wrapper(
  const TappyCallbackWrapperParams<std::function<void(const T&)>>& params, const LoggedMessage& msg)
{
  // If this topic has failed deserialization before do not try deserializing more messages from that topic
  if (params.failed_topics->contains(params.topic))
  {
    return;
  }
  std::unique_ptr<T> deserialized_msg;
  try
  {
    deserialized_msg = std::make_unique<T>();
    if (params.schema_upgrader && params.schema_upgrader->upgrade_required())
    {
      params.schema_upgrader->upgrade(msg.data, std::as_writable_bytes(jewels::as_single_item_span(*deserialized_msg)));
    }
    else
    {
      deserialize_tachyon<T>(*deserialized_msg, msg.data);
    }
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("{}: {}", params.topic, exc.what());
    if (params.error_behavior == DeserializationErrorBehavior::keep_going)
    {
      params.failed_topics->emplace(params.topic);
      return;
    }
    throw;
  }
  params.callback(*deserialized_msg);
}

/// Log processor callback implementation for callback with message and timestamp
/// @tparam T Message type
/// @param[in] params Tappy callback wrapper parameters
/// @param[in] msg Log message
template <typename T>
void tappy_callback_wrapper(
  const TappyCallbackWrapperParams<std::function<void(const LogTimestamp&, const T&)>>& params,
  const LoggedMessage& msg)
{
  // If this topic has failed deserialization before do not try deserializing more messages from that topic
  if (params.failed_topics->contains(params.topic))
  {
    return;
  }
  std::unique_ptr<T> deserialized_msg;
  try
  {
    deserialized_msg = std::make_unique<T>();
    if (params.schema_upgrader && params.schema_upgrader->upgrade_required())
    {
      params.schema_upgrader->upgrade(msg.data, std::as_writable_bytes(std::span{deserialized_msg.get(), 1U}));
    }
    else
    {
      deserialize_tachyon<T>(*deserialized_msg, msg.data);
    }
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("{}: {}", params.topic, exc.what());
    if (params.error_behavior == DeserializationErrorBehavior::keep_going)
    {
      params.failed_topics->emplace(params.topic);
      return;
    }
    throw;
  }
  params.callback(msg.publish_time, *deserialized_msg);
}

} // namespace detail

template <typename T>
LogProcessor& LogProcessor::add_tappy_callback(
  std::string_view topic, std::function<void(const T&)> callback, DeserializationErrorBehavior error_behavior)
  requires clockwork::TappyType<T>
{
  auto callback_params = std::make_shared<detail::TappyCallbackWrapperParams<std::function<void(const T&)>>>(
    std::move(callback),
    std::string(topic),
    error_behavior,
    jewels::memory::make_non_null_from_ref(failed_topics_),
    create_schema_upgrader<T>(topic, error_behavior));
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
  auto wrapped = [callback_params = std::move(callback_params)](const LoggedMessage& msg)
  { detail::tappy_callback_wrapper(*callback_params, msg); };

  return add_raw_msg_callback(topic, std::move(wrapped));
}

template <typename T>
LogProcessor& LogProcessor::add_tappy_callback(
  std::string_view topic,
  std::function<void(const LogTimestamp& publish_time, const T&)> callback,
  DeserializationErrorBehavior error_behavior)
  requires clockwork::TappyType<T>
{
  auto callback_params =
    std::make_shared<detail::TappyCallbackWrapperParams<std::function<void(const LogTimestamp&, const T&)>>>(
      std::move(callback),
      std::string(topic),
      error_behavior,
      jewels::memory::make_non_null_from_ref(failed_topics_),
      create_schema_upgrader<T>(topic, error_behavior));
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks) There is no leak here
  auto wrapped = [callback_params = std::move(callback_params)](const LoggedMessage& msg)
  { detail::tappy_callback_wrapper(*callback_params, msg); };

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

template <typename T>
std::unique_ptr<clockwork::serialization::TachyonUpgrader>
LogProcessor::create_schema_upgrader(std::string_view topic, DeserializationErrorBehavior error_behavior)
  requires clockwork::TappyType<T>
{
  const auto metadata = try_get_topic_metadata(std::string{topic});
  if (!metadata)
  {
    // Channel is not in the log, no need to upgrade the schema
    return {};
  }
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
  try
  {
    upgrader =
      clockwork::serialization::make_tachyon_cpp_upgrader<T>(std::as_bytes(std::span{metadata->schema_definition}));
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("Failed to create upgrader for {}: {}", topic, exc.what());
    if (error_behavior == DeserializationErrorBehavior::throw_exception)
    {
      throw;
    }
    failed_topics_.emplace(topic);
  }
  return upgrader;
}

} // namespace clockwork_logging
