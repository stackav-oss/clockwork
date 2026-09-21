// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>

namespace clockwork_logging
{
/// C++ version of SchemaEncoding, synchronized with the clockwork schema in a unit test
// NOLINTNEXTLINE(performance-enum-size)
WISE_ENUM_CLASS(
  (CppSchemaEncoding, uint16_t),
  (unspecified, 0),
  (undefined, 1),
  (ros2msg, 2),
  (ros2idl, 3),
  (clockwork_tachyon, 4),
  (clockwork_tachyon_zstd, 5))
} // namespace clockwork_logging
