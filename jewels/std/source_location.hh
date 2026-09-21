// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Some versions of clang support -std=c++20 but for some reason leave this in experimental, so
// this provides a canonical version.
#if __has_include(<experimental/source_location>)
#include <experimental/source_location>
#else
#include <source_location>
#endif

namespace jewels
{
#if __has_include(<experimental/source_location>)
using SourceLocation = std::experimental::source_location;
#else
using SourceLocation = std::source_location;
#endif
} // namespace jewels
