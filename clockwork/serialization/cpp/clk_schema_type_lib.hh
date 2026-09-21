// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace clockwork::serialization
{

/// Clockwork field upgrader
class ClkFieldUpgrader
{
public:
  /// Constructor
  /// @param[in] name Name for error messages (schema FQN + field name)
  /// @param[in] src_offset Source field offset
  /// @param[in] src_size Source field size
  /// @param[in] dest_offset Destination field offset
  /// @param[in] dest_size Destination field size
  /// @param[in] upgrader Field value upgrader
  ClkFieldUpgrader(
    std::string_view name,
    size_t src_offset,
    size_t src_size,
    size_t dest_offset,
    size_t dest_size,
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader);

  ~ClkFieldUpgrader() noexcept = default;

  ClkFieldUpgrader(const ClkFieldUpgrader&) noexcept = default;
  ClkFieldUpgrader& operator=(const ClkFieldUpgrader&) noexcept = default;
  ClkFieldUpgrader(ClkFieldUpgrader&&) noexcept = default;
  ClkFieldUpgrader& operator=(ClkFieldUpgrader&&) noexcept = default;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const;

private:
  /// Name for error messages (schema FQN + field name)
  std::string name_;

  /// Source field offset
  size_t src_offset_;

  /// Source field size
  size_t src_size_;

  /// Destination field offset
  size_t dest_offset_;

  /// Destination field size
  size_t dest_size_;

  /// Field value upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader_;
};

/// Initializer for a clockwork schema field
class ClkFieldInitializer
{
public:
  /// Constructor
  /// @param[in] field_offset Field offset
  /// @param[in] field size Field size
  /// @param[in] field_upgrader Field value upgrader
  ClkFieldInitializer(
    size_t field_offset, size_t field_size, jewels::memory::ObjectPtr<const ClkValueInitializer> field_initializer);

  ~ClkFieldInitializer() noexcept = default;

  ClkFieldInitializer(const ClkFieldInitializer&) noexcept = default;
  ClkFieldInitializer& operator=(const ClkFieldInitializer&) noexcept = default;
  ClkFieldInitializer(ClkFieldInitializer&&) noexcept = default;
  ClkFieldInitializer& operator=(ClkFieldInitializer&&) noexcept = default;

  /// Initialize the field value
  /// @param dest_span Field storage to initialize
  void initialize(std::span<std::byte> dest_span) const;

private:
  /// Field offset
  size_t field_offset_;

  /// Field size
  size_t field_size_;

  /// Field initializer
  jewels::memory::ObjectPtr<const ClkValueInitializer> field_initializer_;
};

/// Clockwork field lite-compressor
class ClkFieldLiteCompressor
{
public:
  /// Constructor
  /// @param[in] offset Field offset
  /// @param[in] size Field size
  /// @param[in] compressor Field value compressor
  ClkFieldLiteCompressor(
    size_t offset, size_t size, jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> compressor);

  ~ClkFieldLiteCompressor() noexcept = default;

  ClkFieldLiteCompressor(const ClkFieldLiteCompressor&) noexcept = default;
  ClkFieldLiteCompressor& operator=(const ClkFieldLiteCompressor&) noexcept = default;
  ClkFieldLiteCompressor(ClkFieldLiteCompressor&&) noexcept = default;
  ClkFieldLiteCompressor& operator=(ClkFieldLiteCompressor&&) noexcept = default;

  /// Locate the chunks of zeros that can be replaced with a count in the lite compressed message
  /// @param[in] schema_data Data span containing the schema instance to compress
  /// @param[in] schame_offset Message offset to the schema to compress
  /// @param[in,out] zero_chunks Storage for the zero chunks
  /// @return Success of failure
  jewels::BinaryOutcome compress(
    std::span<const std::byte> schema_data,
    size_t schema_offset,
    jewels::InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const;

  /// Comparison operator for sorting field compressors by field offset so that the chunks of zeros
  /// are returned sorted by the offset into the schema
  [[nodiscard]] friend bool operator<(const ClkFieldLiteCompressor& lhs, const ClkFieldLiteCompressor& rhs) noexcept
  {
    return lhs.offset_ < rhs.offset_;
  }

private:
  /// Field offset
  size_t offset_;

  /// Field size
  size_t size_;

  /// Field value compressor
  jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> compressor_;
};

/// Clockwork schema field definition
class ClkField
{
public:
  /// Constructor
  /// @param[in] offset Field offset
  /// @param[in] num Field number
  /// @param[in] name Field name
  /// @param[in] schema_fqn Fully qualified name of the schema that contains the field
  /// @param[in] type_index Field type index
  /// @param[in] factory Clockwork type factory
  ClkField(
    size_t offset,
    int32_t num,
    std::string_view name,
    std::string_view schema_fqn,
    size_t type_index,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory);

  virtual ~ClkField() = default;

  ClkField(const ClkField&) noexcept = default;
  ClkField& operator=(const ClkField&) noexcept = default;
  ClkField(ClkField&&) noexcept = default;
  ClkField& operator=(ClkField&&) noexcept = default;

  /// Make an initializer function for this field
  /// @return Initializer function or nullopt if field does not require initialization
  [[nodiscard]] virtual std::optional<ClkFieldInitializer> make_initializer();

  /// Get the value initializer for this field
  /// @param[out] initializer_out Output parameter for the initializer
  /// @return success if the field has a value initializer, failure otherwise
  [[nodiscard]] virtual jewels::BinaryOutcome
  get_value_initializer(jewels::FactoryOut<jewels::memory::ObjectPtr<const ClkValueInitializer>> initializer_out) const;

  /// Make an upgrader to upgrade to this field from the source field
  /// @param[in] source_field Field to upgrade from
  /// @return Function to upgrade this field from the source field
  [[nodiscard]] ClkFieldUpgrader make_upgrader(ClkField& src_field);

  /// @return Field offset
  [[nodiscard]] size_t get_offset() const noexcept;

  /// @return Field number
  [[nodiscard]] int32_t get_num() const noexcept;

  /// @return Field name
  [[nodiscard]] std::string_view get_name() const noexcept;

  /// @return Fully qualified name of the schema that contains this field
  [[nodiscard]] std::string_view get_schema_fqn() const noexcept;

  /// @return Field type
  [[nodiscard]] ClkType& get_field_type();

  /// Legacy check for wire compatability for instances written before the built-in type UUID was added to the version
  /// @param[in] src_field Source field to check
  /// @return True if the source field is wire compatibile with this field
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkField& src_field) const;

  /// Check for unexpected schema changes
  /// @param[in] src_field Field to check against
  /// @param[in] name Name for exception strings
  /// @throws runtime_error on unexpected schema changes
  void check_for_unexpected_schema_changes(ClkField& src_field, bool allow_changes, std::string_view name);

  /// Make a compressor for this field
  /// @return Field compressor for this field
  [[nodiscard]] ClkFieldLiteCompressor make_lite_compressor();

  /// @return True iff the field type is compressible
  [[nodiscard]] bool is_compressible();

private:
  /// Field offset
  size_t offset_;

  /// Field number
  int32_t num_;

  /// Field name
  std::string name_;

  /// Fully qualified name of the schema that contains this field
  std::string schema_fqn_;

  /// Field type index
  size_t type_index_;

  /// Clockwork type factory
  jewels::memory::ObjectPtr<ClkTypeFactory> factory_;
};

/// Clockwork schema field that takes an initial value
/// @tparam ValueType Value type
/// @tparam InitialValueType Initial value type
class ClkInitialValueField : public ClkField
{
public:
  /// Constructor
  /// @param[in] offset Field offset
  /// @param[in] num Field number
  /// @param[in] name Field name
  /// @param[in] schema_fqn Fully qualified name of the schema that contains this field
  /// @param[in] type_index Field type index
  /// @param[in] factory Clockwork type factory
  /// @param[in] maybe_initial_value Optional initial value
  ClkInitialValueField(
    size_t offset,
    int32_t num,
    std::string_view name,
    std::string_view schema_fqn,
    size_t type_index,
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    std::optional<metadata::InitialValue> maybe_initial_value);

  ~ClkInitialValueField() override = default;

  ClkInitialValueField(const ClkInitialValueField&) noexcept = default;
  ClkInitialValueField& operator=(const ClkInitialValueField&) noexcept = default;
  ClkInitialValueField(ClkInitialValueField&&) noexcept = default;
  ClkInitialValueField& operator=(ClkInitialValueField&&) noexcept = default;

  /// @see ClkField::make_initializer
  [[nodiscard]] std::optional<ClkFieldInitializer> make_initializer() override;

  /// @see ClkField::get_value_initializer
  [[nodiscard]] jewels::BinaryOutcome get_value_initializer(
    jewels::FactoryOut<jewels::memory::ObjectPtr<const ClkValueInitializer>> initializer_out) const override;

private:
  /// Optional value initializer
  std::shared_ptr<ClkValueInitializer> initializer_;
};

/// Initializer for a clockwork schema
class ClkSchemaInitializer : public ClkValueInitializer
{
public:
  /// Constructor
  /// @param[in] initializers Field initializers
  explicit ClkSchemaInitializer(std::pmr::vector<ClkFieldInitializer> initializers);

  ~ClkSchemaInitializer() override = default;

  ClkSchemaInitializer(const ClkSchemaInitializer&) = delete;
  ClkSchemaInitializer& operator=(const ClkSchemaInitializer&) = delete;
  ClkSchemaInitializer(ClkSchemaInitializer&&) = delete;
  ClkSchemaInitializer& operator=(ClkSchemaInitializer&&) = delete;

  /// @see ClkValueInitializer::initialize
  void initialize(std::span<std::byte> dest_span) const override;

private:
  /// Field initializers
  std::pmr::vector<ClkFieldInitializer> initializers_;
};

/// Clockwork schema argument type
using ClkSchemaArgumentType = std::variant<std::pmr::string, jewels::memory::ObjectPtr<ClkType>>;

/// Clockwork schema type
class ClkSchemaType : public ClkType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] memory_resource Memory resource
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] metadata_version Schema metadata version
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] version Schema version
  /// @param[in] uuid Schema uuid
  ClkSchemaType(
    jewels::memory::MemoryResource memory_resource,
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
    int32_t metadata_version,
    size_t size,
    size_t alignment,
    int32_t version,
    const SchemaUuid& uuid);

  ~ClkSchemaType() override = default;

  ClkSchemaType(const ClkSchemaType&) = delete;
  ClkSchemaType& operator=(const ClkSchemaType&) = delete;
  ClkSchemaType(ClkSchemaType&&) = delete;
  ClkSchemaType& operator=(ClkSchemaType&&) = delete;

  /// Create an instance from a tachyon schema protobuf
  /// @param[in] factory Clockwork type factory
  /// @param[in] schema_proto Tachyon schema protobuf
  /// @param[in] type_index Type index
  /// @param[in] maybe_strong_type_fqn Optional fully qualified name of a strong type wrapping this schema
  /// @returns Clockwork schema instance
  /// @throws runtime_error on failure.
  [[nodiscard]] static std::shared_ptr<ClkSchemaType> from_proto(
    jewels::memory::ObjectPtr<ClkTypeFactory> factory,
    const metadata::SchemaType& schema_proto,
    size_t type_index,
    std::optional<std::string_view> maybe_strong_type_fqn);

  /// @return Type size in bytes
  [[nodiscard]] size_t get_size() const noexcept override;

  /// @return Type alignment in bytes
  [[nodiscard]] size_t get_alignment() const noexcept override;

  /// @return Schema UUID
  [[nodiscard]] const SchemaUuid& get_uuid() const noexcept;

  /// @return Schema version
  [[nodiscard]] int32_t get_version() const noexcept;

  /// @return Schema arguments
  [[nodiscard]] const std::pmr::vector<ClkSchemaArgumentType>& get_arguments() const noexcept;

  /// @return Map from field number to field definition
  [[nodiscard]] const std::pmr::unordered_map<int32_t, std::shared_ptr<ClkField>>& get_fields() const noexcept;

  /// @return Set of fields that have been removed from the current schema
  [[nodiscard]] const std::pmr::set<int32_t>& get_removed() const noexcept;

  /// @return Map from old to new field numbers for fields modified in the current schema
  [[nodiscard]] const std::pmr::map<int32_t, int32_t>& get_became() const noexcept;

  /// @see ClkType::make_initializer
  [[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> make_initializer() override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(ClkType& src_type, bool allow_changes, std::string_view name) override;

  /// @see ClkType::is_same_type
  [[nodiscard]] bool is_same_type(const ClkType& src_type) const override;

  /// @see ClkType::make_lite_compressor
  [[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> make_lite_compressor() override;

  /// @see ClkType::is_compressible
  [[nodiscard]] bool is_compressible() override;

private:
  /// Type size in bytes
  size_t size_;

  /// Type alignment in bytes
  size_t alignment_;

  /// Schema version
  int32_t version_;

  /// Schema UUID
  SchemaUuid uuid_;

  /// Schema arguments
  std::pmr::vector<ClkSchemaArgumentType> arguments_;

  /// Map from field number to field definition
  std::pmr::unordered_map<int32_t, std::shared_ptr<ClkField>> fields_;

  /// Set of fields that have been removed from the current schema
  std::pmr::set<int32_t> removed_;

  /// Map from old to new field numbers for fields modified in the current schema
  std::pmr::map<int32_t, int32_t> became_;

  /// Cached schema initializer
  std::optional<ClkSchemaInitializer> maybe_initializer_;

  /// Flag set when the cached initializer is value
  bool cached_initializer_valid_{false};

  /// Cached results of checks whether to use memcpy for upgrade
  std::pmr::unordered_map<size_t, bool> use_memcpy_cache_;

  /// Cache of checks for unexpected schema changes
  std::pmr::unordered_set<size_t> unexpected_schema_changes_cache_;
};

} // namespace clockwork::serialization
