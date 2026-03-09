// IWYU pragma: private, include "jewels/container/tap/protobuf_to_tap.hh"
#pragma once

#include "jewels/container/tap/protobuf_to_tap.hh"

#include "jewels/container/at.hh"
#include "jewels/container/tap/optional.hh"
#include "jewels/container/tap/var_array.hh"
#include "jewels/container/tap/var_string.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace jewels
{
template <typename Tag>
ConversionStatusExpected protobuf_to_tap(jewels::Uuid<Tag>& output, std::string_view input)
{
  auto maybe_uuid = jewels::Uuid<Tag>::from_string(input);
  if (!maybe_uuid.has_value())
  {
    return jewels::unexpected(jewels::MonoError{});
  }
  output = maybe_uuid.value();
  return ConversionStatusExpected{};
}

template <size_t capacity>
ConversionStatusExpected protobuf_to_tap(tap::VarString<capacity>& output, std::string_view input)
{
  const bool is_success = output.try_set(input);
  if (!is_success)
  {
    jewels::log_cerr_error("Attempted to set a var string with a capacity of {}, with {}", capacity, input);
    return jewels::unexpected(jewels::MonoError{});
  }

  return ConversionStatusExpected{};
}

template <size_t capacity>
ConversionStatusExpected
protobuf_to_tap(jewels::tap::Optional<tap::VarString<capacity>>& output, std::string_view input)
{
  tap::VarString<capacity> var_string;
  const bool is_success = var_string.try_set(input);
  if (!is_success)
  {
    jewels::log_cerr_error("Attempted to set an optional var string with a capacity of {}, with {}", capacity, input);
    return jewels::unexpected(jewels::MonoError{});
  }
  output = var_string;
  return ConversionStatusExpected{};
}

template <size_t capacity>
ConversionStatusExpected protobuf_to_tap(tap::VarArray<std::byte, capacity>& output, std::string_view input)
{
  if (input.size() > capacity)
  {
    jewels::log_cerr_error(
      "Attempted to set a VarArray of bytes with a capacity of {} with a string {} which has {}",
      capacity,
      input,
      input.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  std::transform(
    input.begin(),
    input.end(),
    std::back_inserter(output),
    [](const auto& element) { return static_cast<std::byte>(element); });

  return ConversionStatusExpected{};
}

template <size_t size>
ConversionStatusExpected protobuf_to_tap(std::span<std::byte, size> output, std::string_view input)
{
  if (input.size() > size)
  {
    jewels::log_cerr_error(
      "Attempted to set a span of bytes with capacity {} with a string {} which has {}", size, input, input.size());
    return jewels::unexpected(jewels::MonoError{});
  }
  for (size_t i = 0; i < input.size(); ++i)
  {
    at(output, static_cast<int64_t>(i)) = static_cast<std::byte>(input.at(i));
  }
  return ConversionStatusExpected{};
}

template <jewels::meta::Integral From, jewels::meta::Integral To>
ConversionStatusExpected protobuf_to_tap(To& output, From input)
{
  // Normalize so that "max" is unsigned and "min" is signed
  constexpr auto min_from = static_cast<std::make_signed_t<From>>(std::numeric_limits<From>::lowest());
  constexpr auto max_from = static_cast<std::make_unsigned_t<From>>(std::numeric_limits<From>::max());
  constexpr auto min_to = static_cast<std::make_signed_t<To>>(std::numeric_limits<To>::lowest());
  constexpr auto max_to = static_cast<std::make_unsigned_t<To>>(std::numeric_limits<To>::max());
  if constexpr (max_from > max_to)
  {
    if (input > 0 && static_cast<std::make_unsigned_t<From>>(input) > max_to)
    {
      jewels::log_cerr_error("Integer value in protobuf {} is out of range for converted type", input);
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  if constexpr (std::is_signed_v<From> && min_from < min_to)
  {
    if (input < min_to)
    {
      jewels::log_cerr_error("Integer value in protobuf {} is out of range for converted type", input);
      return jewels::unexpected(jewels::MonoError{});
    }
  }
  output = static_cast<To>(input);
  return ConversionStatusExpected{};
}

} // namespace jewels
