// IWYU pragma: private, include "jewels/uuid/uuid.hh"
#pragma once

#include "jewels/uuid/uuid.hh"

#include "jewels/container/at.hh"
#include "jewels/memory/fields.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"
#include "jewels/std/span.hh"

#include <fmt/base.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <iterator>
#include <memory_resource>
#include <mutex>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace jewels
{

namespace detail
{

template <typename OutIter>
constexpr void write_hex_chars(OutIter out, uint8_t value, bool upper)
{
  constexpr uint8_t hex_digit_mask = 0xF;
  constexpr uint8_t hex_digit_0_shift = 0;
  constexpr uint8_t hex_digit_1_shift = 4;
  constexpr std::string_view uppercase_digits = "0123456789ABCDEF";
  constexpr std::string_view lowercase_digits = "0123456789abcdef";
  const std::string_view& chars = (upper ? uppercase_digits : lowercase_digits);
  *out++ = chars[static_cast<uint8_t>(value >> hex_digit_1_shift) & hex_digit_mask];
  *out++ = chars[static_cast<uint8_t>(value >> hex_digit_0_shift) & hex_digit_mask];
}

/// Convert a hexadecimal character to uint8_t
/// @param[in] n Character to convert
/// @return Decoded hex value or error string on failure
[[nodiscard]] constexpr jewels::expected<uint8_t, std::string_view> decode_hex_char(const char n)
{
  const auto max_decimal = 10U;
  if (n >= '0' && n <= '9')
  {
    return static_cast<uint8_t>(n - '0');
  }
  if (n >= 'a' && n <= 'f')
  {
    return static_cast<uint8_t>(n - 'a') + max_decimal;
  }
  if (n >= 'A' && n <= 'F')
  {
    return static_cast<uint8_t>(n - 'A') + max_decimal;
  }
  return jewels::unexpected("Invalid UUID string: could not decode hex character");
}

/// Convert the first two hex characters from a string view to uint8_t
/// @param[in] str String view
/// @return Decoded hex value or error string on failure
[[nodiscard]] constexpr jewels::expected<uint8_t, std::string_view> decode_hex_chars(const std::string_view str)
{
  if (str.size() < 2)
  {
    return jewels::unexpected("Invalid UUID string: hit end of string trying to decode hex");
  }
  const auto hex1 = decode_hex_char(str[0U]);
  if (!hex1)
  {
    return jewels::unexpected(hex1.error());
  }
  const auto hex2 = decode_hex_char(str[1U]);
  if (!hex2)
  {
    return jewels::unexpected(hex2.error());
  }
  return static_cast<uint8_t>(*hex1 << 4U) + *hex2;
}

/// Parse a certain number of bytes (i.e. two hex characters) from a UUID string.
/// @param[in] str String view, which will be read from the beginning (i.e. it should be a substring)
/// @param[in] num_bytes Number of bytes to parse
/// @param[out] uuid The UUID to store into
/// @param[in] uuid_offset The offset into the UUID buffer to store into
/// @return string view of remaining string, or error string on failure
template <typename TagType>
[[nodiscard]] constexpr jewels::expected<std::string_view, std::string_view>
parse_uuid_bytes_from_string(std::string_view str, const uint8_t num_bytes, Uuid<TagType>& uuid, uint32_t uuid_offset)
{
  for (std::ptrdiff_t i = uuid_offset; std::cmp_less(i, uuid_offset + num_bytes); ++i)
  {
    const auto hex_value = detail::decode_hex_chars(str);
    if (!hex_value)
    {
      return jewels::unexpected(hex_value.error());
    }
    jewels::at(uuid.uuid, i) = *hex_value;
    str = str.substr(2U);
  }
  return str;
}

/// Checks to see whether the first character of the UUID string is a dash if expect_dashes is true.
/// @param[in] str String view to examine
/// @param[in] expect_dashes Whether to expect dashes
/// @returns A string view, offset by one character if it started with a dash, or an error
[[nodiscard]] constexpr jewels::expected<std::string_view, std::string_view>
check_for_dash(const std::string_view str, const bool expect_dashes)
{
  if (str.empty())
  {
    return jewels::unexpected("Invalid UUID string: hit end of string");
  }

  if (expect_dashes)
  {
    if (str[0] != '-')
    {
      return jewels::unexpected("Invalid UUID string: missing dash after section");
    }
    return str.substr(1);
  }

  return str;
}

/// Generator for seeds to initialize the random number generator
struct SeedGenerator
{
  using result_type = uint32_t;

  /// Generate a sequence of seeds to initialize the random number generator
  /// @tparam IterType Iterator type
  /// @param[in] begin Iterator to the beginning of the range
  /// @param[in] end Iterator to the end of the range
  template <typename IterType>
  void generate(IterType begin, IterType end) noexcept;
};

template <typename IterType>
void SeedGenerator::generate(IterType begin, IterType end) noexcept
{
  std::random_device random_dev{};
  for (auto iter = begin; iter < end; ++iter)
  {
    *iter = random_dev();
  }
}

/// Fill a span of bytes with random data
/// @param[in,out] data Data to fill
inline void fill_with_random_bytes(std::span<std::byte> data) noexcept
{
  static detail::SeedGenerator seed_gen;
  static std::ranlux48 gen{seed_gen};
  static std::uniform_int_distribution<uint8_t> distrib{};
  static std::mutex mutex;
  const std::lock_guard guard{mutex};
  for (auto& element : data)
  {
    element = static_cast<std::byte>(distrib(gen));
  }
}
} // namespace detail

template <typename TagType>
constexpr Uuid<TagType>::Uuid(const std::array<uint8_t, uuid_size_bytes>& uuid_in) noexcept
  : uuid(uuid_in)
{
}

template <typename TagType>
[[nodiscard]] bool Uuid<TagType>::is_nil() const noexcept
{
  static const Uuid nil_uuid;
  return *this == nil_uuid;
}

template <typename TagType>
[[nodiscard]] std::string Uuid<TagType>::to_string() const
{
  return fmt::format("{}", *this);
}

template <typename TagType>
[[nodiscard]] std::pmr::string Uuid<TagType>::to_string(memory::MemoryResource memory_resource) const
{
  std::pmr::string uuid_str{memory_resource};
  fmt::format_to(std::back_inserter(uuid_str), "{}", *this);
  return uuid_str;
}

template <typename TagType>
[[nodiscard]] constexpr jewels::expected<Uuid<TagType>, std::string_view>
Uuid<TagType>::from_string(std::string_view str)
{
  bool expect_end_brace = false;
  bool expect_dashes = false;
  if (str.empty())
  {
    return jewels::unexpected("Invalid UUID string: empty");
  }
  if (str[0] == '{')
  {
    expect_end_brace = true;
    str = str.substr(1);
  }

  Uuid uuid{};
  auto uuid_offset = 0U;

  const auto first_field_width = 4U;
  auto new_str = detail::parse_uuid_bytes_from_string(str, first_field_width, uuid, uuid_offset);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;
  uuid_offset += first_field_width;

  if (str.empty())
  {
    return jewels::unexpected("Invalid UUID string: no data after first field");
  }
  if (str[0] == '-')
  {
    expect_dashes = true;
    str = str.substr(1);
  }

  const auto second_field_width = 2U;
  new_str = detail::parse_uuid_bytes_from_string(str, second_field_width, uuid, uuid_offset);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;
  uuid_offset += second_field_width;

  new_str = detail::check_for_dash(str, expect_dashes);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;

  const auto third_field_width = 2U;
  new_str = detail::parse_uuid_bytes_from_string(str, third_field_width, uuid, uuid_offset);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;
  uuid_offset += third_field_width;

  new_str = detail::check_for_dash(str, expect_dashes);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;

  const auto fourth_field_width = 2U;
  new_str = detail::parse_uuid_bytes_from_string(str, fourth_field_width, uuid, uuid_offset);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;
  uuid_offset += fourth_field_width;

  new_str = detail::check_for_dash(str, expect_dashes);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;

  const auto fifth_field_width = 6U;
  new_str = detail::parse_uuid_bytes_from_string(str, fifth_field_width, uuid, uuid_offset);
  if (!new_str)
  {
    return jewels::unexpected(new_str.error());
  }
  str = *new_str;
  uuid_offset += fifth_field_width;

  if (expect_end_brace)
  {
    if (str.empty())
    {
      return jewels::unexpected("Invalid UUID string: expected ending brace but hit end of string");
    }
    if (str[0] != '}')
    {
      return jewels::unexpected("Invalid UUID string: expected ending brace");
    }
    str = str.substr(1);
  }
  if (!str.empty())
  {
    return jewels::unexpected("Invalid UUID string: extra data at end of string");
  }
  return uuid;
}

template <typename TagType>
[[nodiscard]] Uuid<TagType> Uuid<TagType>::random_uuid() noexcept
{
  // Conforming to RFC 9652 section 5.7 (UUIDv7)
  Uuid uuid{};

  // 48 bits of big-endian time since epoch in milliseconds
  const auto big_endian_ms = memory::ToBigEndian{}(static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count()));
  const auto time_span = std::as_bytes(jewels::as_single_item_span(big_endian_ms)).last(time_size_bytes);
  std::ranges::copy(time_span, std::as_writable_bytes(std::span(uuid.uuid).first(time_size_bytes)).begin());

  // 80 bits of pseudo-random data
  detail::fill_with_random_bytes(std::as_writable_bytes(std::span(uuid.uuid).subspan(time_size_bytes)));

  // Fill in UUID version (7)
  constexpr uint8_t version_byte_index = 6U;
  constexpr uint8_t version_byte_mask = 0x0FU;
  constexpr uint8_t version = 7U; // UUID version 7
  constexpr uint8_t version_shift = 4U;
  uuid.uuid[version_byte_index] = static_cast<uint8_t>(uuid.uuid[version_byte_index] & version_byte_mask) |
                                  static_cast<uint8_t>(version << version_shift);

  // Fill in UUID variant (b10)
  constexpr uint8_t var_byte_index = 8U;
  constexpr uint8_t var_byte_mask = 0x3FU;
  constexpr uint8_t var_bits = 0x80U;
  uuid.uuid[var_byte_index] = static_cast<uint8_t>(uuid.uuid[var_byte_index] & var_byte_mask) | var_bits;

  return uuid;
}

template <typename TagType>
std::ostream& operator<<(std::ostream& ostream, const Uuid<TagType>& value)
{
  ostream << value.to_string();
  return ostream;
}

} // namespace jewels

template <typename TagType, typename Char>
struct fmt::formatter<jewels::Uuid<TagType>, Char>
{
  bool uppercase = false;

  template <class ParseContext>
  constexpr ParseContext::iterator parse(ParseContext& ctx)
  {
    auto iter = ctx.begin();
    if (iter != ctx.end())
    {
      if (*iter == 'x' || *iter == 'X')
      {
        uppercase = (*iter == 'X');
        ++iter; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) TODO(OI-3647)
      }
    }
    if (iter != ctx.end() && *iter != '}')
    {
      throw std::runtime_error("Invalid format args for Uuid.");
    }
    return iter;
  }

  template <class FmtContext>
  constexpr FmtContext::iterator format(jewels::Uuid<TagType> uuid, FmtContext& ctx) const
  {
    for (size_t i = 0; i < uuid.uuid.size(); i++)
    {
      // NOLINTNEXTLINE(readability-magic-numbers): positions of dashes in normal uuid format
      if (i == 4 || i == 6 || i == 8 || i == 10)
      {
        *ctx.out()++ = '-';
      }
      jewels::detail::write_hex_chars(ctx.out(), uuid.uuid.at(i), uppercase);
    }
    return ctx.out();
  }
};
