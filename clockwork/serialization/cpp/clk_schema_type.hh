// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/cpp/clk_type.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <string_view>

namespace clockwork::serialization
{

/// Clockwork schema type factory plugin
class ClkSchemaTypeFactoryPlugin : public ClkTypeFactoryPlugin
{
public:
  ClkSchemaTypeFactoryPlugin() noexcept = default;

  ~ClkSchemaTypeFactoryPlugin() override = default;

  ClkSchemaTypeFactoryPlugin(const ClkSchemaTypeFactoryPlugin&) = delete;
  ClkSchemaTypeFactoryPlugin& operator=(const ClkSchemaTypeFactoryPlugin&) = delete;
  ClkSchemaTypeFactoryPlugin(ClkSchemaTypeFactoryPlugin&&) = delete;
  ClkSchemaTypeFactoryPlugin& operator=(ClkSchemaTypeFactoryPlugin&&) = delete;

  /// @see ClkTypeFactoryPlugin::make_clk_type
  [[nodiscard]] std::unique_ptr<ClkType> make_clk_type(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::TypeDesc& type_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn) const override;
};

} // namespace clockwork::serialization
