// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/container/compare.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/std/expected.hh"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace jewels
{

/// Alignment of a uuid type.  Must be consistent with the clockwork::Tachyon
/// registry.
inline constexpr auto uuid_alignment{8UL};

/// UUID size in bytes
inline constexpr size_t uuid_size_bytes = 16U;

/// Tagged UUID class
/// @tparam TagType Tag type
template <typename TagType>
struct alignas(uuid_alignment) Uuid
{
  /// UUID size in bytes
  static constexpr size_t uuid_size_bytes = 16U;

  /// Construct a UUID from raw bytes
  /// @param[in] uuid_in Raw UUID bytes
  constexpr explicit Uuid(const std::array<uint8_t, uuid_size_bytes>& uuid_in) noexcept;

  /// Default constructor makes a nil UUID
  Uuid() noexcept = default;

  ~Uuid() noexcept = default;
  Uuid(const Uuid&) noexcept = default;
  Uuid& operator=(const Uuid&) noexcept = default;
  Uuid(Uuid&&) noexcept = default;
  Uuid& operator=(Uuid&&) noexcept = default;

  /// Test whether a UUID is nil (all zeros)
  /// @return True iff the UUID is nil
  [[nodiscard]] bool is_nil() const noexcept;

  /// Get the string representation of the UUID in hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh format
  /// @return UUID string
  [[nodiscard]] std::string to_string() const;

  /// Get the string representation of the UUID in hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh format
  /// @return UUID string
  [[nodiscard]] std::pmr::string to_string(memory::MemoryResource memory_resource) const;

  /// Set the UUID from it's string representation
  ///
  /// Accepts the same string formats as boost::uuids::uuid
  ///
  ///  hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh
  ///  {hhhhhhhh-hhhh-hhhh-hhhh-hhhhhhhhhhhh}
  ///  hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh
  ///  {hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh}
  ///
  /// @param[in] str UUID string representation
  /// @return UUID value or error string on failure
  [[nodiscard]] static constexpr jewels::expected<Uuid, std::string_view> from_string(std::string_view str);

  /// Generate a random log UUID
  /// @return random UUID
  [[nodiscard]] static Uuid random_uuid() noexcept;

  /// Equality comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend constexpr bool operator==(const Uuid& lhs, const Uuid& rhs) noexcept
  {
    return jewels::array_eq(lhs.uuid, rhs.uuid);
  }

  /// Inequality comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs != rhs
  [[nodiscard]] friend constexpr bool operator!=(const Uuid& lhs, const Uuid& rhs) noexcept
  {
    return !jewels::array_eq(lhs.uuid, rhs.uuid);
  }

  /// Less than comparison operator
  /// @param[in] lhs Left hand operand
  /// @param[in] rhs Right hand operand
  /// @return True iff lhs == rhs
  [[nodiscard]] friend constexpr bool operator<(const Uuid& lhs, const Uuid& rhs) noexcept
  {
    return jewels::array_lt(lhs.uuid, rhs.uuid);
  }

  /// UUID storage
  std::array<uint8_t, uuid_size_bytes> uuid{};
};

/// Output stream insertion operator for log UUID values
/// @tparam TagType Tag type
/// @param[in] ostream Output stream
/// @param[in] value UUID value
/// @return Output stream reference
template <typename TagType>
std::ostream& operator<<(std::ostream& ostream, const Uuid<TagType>& value);
} // namespace jewels

#include "jewels/uuid/uuid.inl"
