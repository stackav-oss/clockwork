// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_uuid_tag_clk_cc.hh"
#include "jewels/uuid/uuid.hh"
#include "jewels/uuid/uuid_hasher.hh"
namespace clockwork_logging
{

/// Logging UUID class
using LogUuid = jewels::Uuid<tags::LogUuidTag>;

using LogUuidHasher = jewels::UuidHasher<tags::LogUuidTag>;

} // namespace clockwork_logging
