// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <wise_enum.h>

#include <cstdint>

namespace clockwork_logging
{

/// C++ version of MessageEncoding, synchronized with the clockwork schema in a unit test
// NOLINTNEXTLINE(performance-enum-size)
WISE_ENUM_CLASS((CppMessageEncoding, uint16_t), (unspecified, 0), (undefined, 1), (cdr, 2), (tachyon, 3))

} // namespace clockwork_logging
