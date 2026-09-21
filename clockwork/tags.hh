// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork
{

/// A tag representing a schema.  This can be used for things like
/// jewels::Uuid<SchemaTag> to indicate the UUID is for a schema class type.
/// Because schemas can be instantiated with different parameters,
/// most code should use the RepresentationTag ids instead
struct SchemaTag
{
};

/// A tag representing a particular representation of a schema.  This
/// is intended for use as jewels::Uuid<RepresentationTag> to uniquely identify
/// a specific message type (e.g. a protobuf version of a particular
/// schema instance).
struct RepresentationTag
{
};

} // namespace clockwork
