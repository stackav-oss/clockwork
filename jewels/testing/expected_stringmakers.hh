// Copyright 2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

/// Catch2 string makers for jewels types.
#pragma once

#include "jewels/std/expected.hh"

#include <catch2/catch_tostring.hpp>

#include <string>

namespace Catch
{
/// Specialization of Catch2's `StringMaker` for `jewels::expected`.
template <typename Result, typename Error>
struct StringMaker<jewels::expected<Result, Error>>
{
  static std::string convert(const jewels::expected<Result, Error>& value);
};

/// Specialization of Catch2's `StringMaker` for `jewels::MonoError`.
template <>
struct StringMaker<jewels::MonoError>
{
  static std::string convert(const jewels::MonoError& /*value*/);
};
} // namespace Catch

#include "jewels/testing/expected_stringmakers.inl"
