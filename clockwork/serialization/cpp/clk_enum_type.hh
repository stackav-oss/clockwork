// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/cpp/clk_type.hh"
#include "jewels/memory/pointers.hh"

#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace clockwork::serialization
{

/// Clockwork enum options
WISE_ENUM_CLASS(
  (ClkEnumOption, uint8_t),
  /// Flag option, value is a bit mask
  flag)

/// Clockwork schema type factory plugin
class ClkEnumTypeFactoryPlugin : public ClkTypeFactoryPlugin
{
public:
  ClkEnumTypeFactoryPlugin() noexcept = default;

  ~ClkEnumTypeFactoryPlugin() override = default;

  ClkEnumTypeFactoryPlugin(const ClkEnumTypeFactoryPlugin&) = delete;
  ClkEnumTypeFactoryPlugin& operator=(const ClkEnumTypeFactoryPlugin&) = delete;
  ClkEnumTypeFactoryPlugin(ClkEnumTypeFactoryPlugin&&) = delete;
  ClkEnumTypeFactoryPlugin& operator=(ClkEnumTypeFactoryPlugin&&) = delete;

  /// @see ClkTypeFactoryPlugin::make_clk_type
  [[nodiscard]] std::shared_ptr<ClkType> make_clk_type(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::TypeDesc& type_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn) const override;
};

} // namespace clockwork::serialization
