// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"

#include <cstddef>
#include <span>
#include <string_view>

/// Lint error helpers for logging code

namespace clockwork_logging::nolint_helper
{

/// Convert a span of bytes to a string view
/// @param[in] data Span of bytes
/// @return String view containing the data
[[nodiscard]] inline std::string_view byte_span_to_string_view(std::span<const std::byte> data);

/// Convert a span of bytes to a span of values
///
/// The value span size if computed by dividing the input span size by the value size and rounding down
/// to the nearest integer.
///
/// @tparam T Value type
/// @param[in] data Span of bytes
/// @return String view containing the data
template <typename T>
[[nodiscard]] std::span<const T> byte_span_to_value_span(std::span<const std::byte> data);

/// Convert a span of bytes to a mutable span of values
///
/// The value span size if computed by dividing the input span size by the value size and rounding down
/// to the nearest integer.
///
/// @tparam T Value type
/// @param[in] data Span of bytes
/// @return String view containing the data
template <typename T>
[[nodiscard]] std::span<T> byte_span_to_value_span(std::span<std::byte> data);

/// Convert a span of bytes to a mutable value pointer
/// @tparam T Value type
/// @param[in] data Span of bytes
/// @return Pointer to the value of invalid_argument if the span size doesn't match the value size
template <typename T>
[[nodiscard]] LogExpected<T*> byte_span_to_mutable_value_ptr(std::span<std::byte> data);

/// Convert a span of bytes to a const value pointer
/// @tparam T Value type
/// @param[in] data Span of bytes
/// @return Pointer to the value of invalid_argument if the span size doesn't match the value size
template <typename T>
[[nodiscard]] LogExpected<const T*> byte_span_to_value_ptr(std::span<const std::byte> data);

/// Get a char pointer to an element in a span of bytes, possibly past the end of the span
/// @param[in] data span of bytes
/// @param[in] offset Offset to the element
/// @return Char pointer to element at the offset
[[nodiscard]] inline char* char_ptr_to_span_element(std::span<std::byte> data, size_t offset = 0U);

/// Workaround to do pointer arithmentic for stream buffers
/// @param[in] ptr Char pointer
/// @param[in] count Number of bytes to add to the pointer
/// @return Pointer to 'ptr' + count
[[nodiscard]] inline char* increment_char_ptr(char* ptr, size_t count);

/// Calling std::getenv is thread safe after C++11 as long as nothing is modifying the environment
/// @param[in] env_var Environment variable name
/// @return Environment variable value
[[nodiscard]] inline const char* get_environment_variable(const char* env_var);

/// Calling setenv is thread safe after C++11 as long as nothing is modifying the environment
/// @param[in] env_var Environment variable name
/// @param[in] env_val Environment variable value
inline void set_environment_variable(const char* env_var, const char* env_val);

} // namespace clockwork_logging::nolint_helper

#include "clockwork/logging/nolint_helper.inl"
