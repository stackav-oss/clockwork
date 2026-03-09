// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_schema_type.hh"

#include "clockwork/serialization/cpp/clk_schema_type_lib.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace clockwork::serialization
{

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkType> ClkSchemaTypeFactoryPlugin::make_clk_type(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::TypeDesc& type_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn) const
{
  if (!type_proto.has_schema())
  {
    return nullptr;
  }
  return ClkSchemaType::from_proto(factory, type_proto.schema(), type_index, maybe_strong_type_fqn);
}

} // namespace clockwork::serialization
