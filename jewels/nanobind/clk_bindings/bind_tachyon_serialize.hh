// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/repr_iface.hh"

#include <nanobind/nanobind.h>

#include <cstdint>

namespace jewels::nanobind
{

/// Add methods related to tachyon serialization to a clockwork::Tappy schema nanobinding:
/// * serialize_tachyon
/// * deserialize_tachyon
/// * __getstate__
/// * __setstate__
template <typename SchemaT>
void bind_tachyon_serialize(::nanobind::class_<clockwork::Tappy<SchemaT>>& cls);

/// Bind 'get_tachyon_constraint' and 'get_tachyon_metadata' to finish implementing
/// the 'clockwork.serialization.py.protocol.clockwork::Tachyon' protocol.
/// The tachyon constraints are sanity checked against sizeof() and alignof().
template <typename SchemaT>
void bind_tachyon_constraint_and_metadata(
  ::nanobind::class_<clockwork::Tappy<SchemaT>>& cls,
  int64_t tachyon_constraint_size,
  int64_t tachyon_constraint_alignment,
  std::string_view tachyon_module_name,
  std::string_view tachyon_source_file_name,
  std::string_view tachyon_class_name);

} // namespace jewels::nanobind

#include "jewels/nanobind/clk_bindings/bind_tachyon_serialize.inl"
