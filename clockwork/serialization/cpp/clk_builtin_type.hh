// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/cpp/clk_type.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <string_view>

namespace clockwork::serialization
{

/// Calculate the offset to the size value in a VarArrayType
/// @param[in] array_size Maximum number of elements in the array
/// @param[in] element_size Array element size in bytes
/// @return Offset to the size value in the array storage
[[nodiscard]] size_t var_array_size_offset(size_t array_size, size_t element_size);

/// Calculate the offset to the has_value flag in an OptionalType
/// @param[in] value_size Optional element size in bytes
/// @return Offset to the flag value in the optional storage
[[nodiscard]] size_t optional_has_value_offset(size_t value_size);

/// Clockwork built-in type factory plugin
class ClkBuiltInTypeFactoryPlugin : public ClkTypeFactoryPlugin
{
public:
  ClkBuiltInTypeFactoryPlugin() noexcept = default;

  ~ClkBuiltInTypeFactoryPlugin() override = default;

  ClkBuiltInTypeFactoryPlugin(const ClkBuiltInTypeFactoryPlugin&) = delete;
  ClkBuiltInTypeFactoryPlugin& operator=(const ClkBuiltInTypeFactoryPlugin&) = delete;
  ClkBuiltInTypeFactoryPlugin(ClkBuiltInTypeFactoryPlugin&&) = delete;
  ClkBuiltInTypeFactoryPlugin& operator=(ClkBuiltInTypeFactoryPlugin&&) = delete;

  /// @see ClkTypeFactoryPlugin::make_clk_type
  [[nodiscard]] std::unique_ptr<ClkType> make_clk_type(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::TypeDesc& type_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn) const override;
};

} // namespace clockwork::serialization
