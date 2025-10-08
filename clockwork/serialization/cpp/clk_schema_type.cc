// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_schema_type.hh"

#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/pointers.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt10/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace clockwork::serialization
{

namespace
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

ClkFieldUpgrader::ClkFieldUpgrader(
  std::string_view name,
  size_t src_offset,
  size_t src_size,
  size_t dest_offset,
  size_t dest_size,
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader)
  : name_(name),
    src_offset_(src_offset),
    src_size_(src_size),
    dest_offset_(dest_offset),
    dest_size_(dest_size),
    upgrader_(upgrader)
{
}

void ClkFieldUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  try
  {
    upgrader_->upgrade(src_span.subspan(src_offset_, src_size_), dest_span.subspan(dest_offset_, dest_size_));
  }
  catch (const ClkTypeUpgradeError& exc)
  {
    throw ClkSchemaFieldUpgradeError(fmt::format("Failed to upgrade {}: {}", name_, exc.what()));
  }
}

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

ClkFieldInitializer::ClkFieldInitializer(
  size_t field_offset, size_t field_size, jewels::memory::ObjectPtr<const ClkValueInitializer> field_initializer)
  : field_offset_(field_offset), field_size_(field_size), field_initializer_(field_initializer)
{
}

void ClkFieldInitializer::initialize(std::span<std::byte> dest_span) const
{
  field_initializer_->initialize(dest_span.subspan(field_offset_, field_size_));
}

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
  void check_for_unexpected_schema_changes(ClkField& src_field, std::string_view name);

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

ClkField::ClkField(
  size_t offset,
  int32_t num,
  std::string_view name,
  std::string_view schema_fqn,
  size_t type_index,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory)
  : offset_(offset), num_(num), name_(name), schema_fqn_(schema_fqn), type_index_(type_index), factory_(factory)
{
}

[[nodiscard]] size_t ClkField::get_offset() const noexcept
{
  return offset_;
}

[[nodiscard]] int32_t ClkField::get_num() const noexcept
{
  return num_;
}

[[nodiscard]] std::string_view ClkField::get_name() const noexcept
{
  return name_;
}

[[nodiscard]] std::string_view ClkField::get_schema_fqn() const noexcept
{
  return schema_fqn_;
}

[[nodiscard]] ClkType& ClkField::get_field_type()
{
  return *factory_->get_clk_type(type_index_);
}

[[nodiscard]] std::optional<ClkFieldInitializer> ClkField::make_initializer()
{
  auto& field_type = get_field_type();
  if (auto maybe_initializer = field_type.make_initializer(); maybe_initializer)
  {
    return ClkFieldInitializer{get_offset(), field_type.get_size(), *maybe_initializer};
  }
  return std::nullopt;
}

[[nodiscard]] bool ClkField::is_legacy_wire_compatible(const ClkField& src_field) const
{
  return src_field.num_ == num_ && src_field.name_ == name_ && src_field.offset_ == offset_ &&
         src_field.type_index_ == type_index_;
}

void ClkField::check_for_unexpected_schema_changes(ClkField& src_field, std::string_view name)
{
  if (src_field.num_ == num_)
  {
    const auto field_name = fmt::format("{}.{}", name, name_);
    if (src_field.name_ != name_)
    {
      throw ClkTypeUpgradeError(
        fmt::format("Field {} renamed to {} in {} without changing field number", src_field.name_, name_, name));
    }
    get_field_type().check_for_unexpected_schema_changes(src_field.get_field_type(), field_name);
  }
}

[[nodiscard]] ClkFieldUpgrader ClkField::make_upgrader(ClkField& src_field)
{
  return ClkFieldUpgrader{
    fmt::format("{}.{}", schema_fqn_, name_),
    src_field.get_offset(),
    src_field.get_field_type().get_size(),
    get_offset(),
    get_field_type().get_size(),
    get_field_type().make_upgrader(src_field.get_field_type())};
}

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

private:
  /// Optional value initializer
  std::shared_ptr<ClkValueInitializer> initializer_;
};

ClkInitialValueField::ClkInitialValueField(
  size_t offset,
  int32_t num,
  std::string_view name,
  std::string_view schema_fqn,
  size_t type_index,
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  std::optional<metadata::InitialValue> maybe_initial_value)
  : ClkField(offset, num, name, schema_fqn, type_index, factory)
{
  if (maybe_initial_value)
  {
    initializer_ = get_field_type().make_value_initializer(maybe_initial_value.value());
  }
}

[[nodiscard]] std::optional<ClkFieldInitializer> ClkInitialValueField::make_initializer()
{
  if (initializer_)
  {
    return ClkFieldInitializer{
      get_offset(), get_field_type().get_size(), jewels::memory::make_non_null_from_ref(*initializer_)};
  }
  return std::nullopt;
}

/// Construct a clockwork schema field from a protobuf
/// @param[in] factory Clockwork type factory
/// @param[in] schema_fqn Fully qualified name of the schema that contains the field
/// @param[in] field_proto Tachyon schema field protobuf
/// @return Clockwork schema field instance
// Recursion needed because types are defined recursively.
// Functional complexity is due to the switch statement that needs to handle all of the field types
// NOLINTNEXTLINE(misc-no-recursion, readability-function-cognitive-complexity) See above
std::unique_ptr<ClkField> clk_field_from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  std::string_view schema_fqn,
  const metadata::SchemaField& field_proto)
{
  const auto& field_type = *factory->get_clk_type(static_cast<size_t>(field_proto.type_id()));
  switch (field_type.get_type_id())
  {
  case ClkTypeId::boolean:
    [[fallthrough]];
  case ClkTypeId::uint8:
  case ClkTypeId::uint16:
  case ClkTypeId::uint32:
  case ClkTypeId::uint64:
  case ClkTypeId::int8:
  case ClkTypeId::int16:
  case ClkTypeId::int32:
  case ClkTypeId::int64:
  case ClkTypeId::float32:
  case ClkTypeId::float64:
  case ClkTypeId::clk_enum:
    return std::make_unique<ClkInitialValueField>(
      static_cast<size_t>(field_proto.offset()),
      field_proto.num(),
      field_proto.name(),
      schema_fqn,
      static_cast<size_t>(field_proto.type_id()),
      factory,
      field_proto.has_init_value() ? std::optional<metadata::InitialValue>{field_proto.init_value()}
                                   : std::optional<metadata::InitialValue>{});
  case ClkTypeId::synctime:
    [[fallthrough]];
  case ClkTypeId::duration:
  case ClkTypeId::byte:
  case ClkTypeId::uuid:
  case ClkTypeId::fixed_array:
  case ClkTypeId::var_array:
  case ClkTypeId::var_string:
  case ClkTypeId::optional:
  case ClkTypeId::schema:
    return std::make_unique<ClkField>(
      static_cast<size_t>(field_proto.offset()),
      field_proto.num(),
      field_proto.name(),
      schema_fqn,
      static_cast<size_t>(field_proto.type_id()),
      factory);
  default:
    throw ClkTypeUpgradeError(fmt::format("Unhandled clockwork type: {}", field_type.get_type_id()));
  }
}

/// Clockwork upgrader for schema types
class ClkSchemaUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] initializers Schema field initializers
  /// @param[in] upgraders Schema field upgraders
  ClkSchemaUpgrader(std::vector<ClkFieldInitializer> initializers, std::vector<ClkFieldUpgrader> upgraders);

  ~ClkSchemaUpgrader() noexcept override = default;

  ClkSchemaUpgrader(const ClkSchemaUpgrader&) = delete;
  ClkSchemaUpgrader& operator=(const ClkSchemaUpgrader&) = delete;
  ClkSchemaUpgrader(ClkSchemaUpgrader&&) = delete;
  ClkSchemaUpgrader& operator=(ClkSchemaUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Field initializers
  std::vector<ClkFieldInitializer> initializers_;

  /// Field upgraders
  std::vector<ClkFieldUpgrader> upgraders_;
};

ClkSchemaUpgrader::ClkSchemaUpgrader(
  std::vector<ClkFieldInitializer> initializers, std::vector<ClkFieldUpgrader> upgraders)
  : initializers_(std::move(initializers)), upgraders_(std::move(upgraders))
{
}

void ClkSchemaUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  for (const auto& initializer : initializers_)
  {
    initializer.initialize(dest_span);
  }
  for (const auto& upgrader : upgraders_)
  {
    upgrader.upgrade(src_span, dest_span);
  }
}

/// Clockwork value enum upgrader
/// @tparam SrcValueType Source enum value type
/// @tparam DestValueType Destination enum value type
template <typename SrcValueType, typename DestValueType>
class ClkValueEnumUpgrader : public ClkTypeUpgrader
{
public:
  /// Constructor
  /// @param[in] src_fqn Source enum FQN
  /// @param[in] value_map Map from source enum value to destination enum value
  ClkValueEnumUpgrader(std::string_view src_fqn, std::unordered_map<SrcValueType, DestValueType> value_map) noexcept;

  ~ClkValueEnumUpgrader() noexcept override = default;

  ClkValueEnumUpgrader(const ClkValueEnumUpgrader&) = delete;
  ClkValueEnumUpgrader& operator=(const ClkValueEnumUpgrader&) = delete;
  ClkValueEnumUpgrader(ClkValueEnumUpgrader&&) = delete;
  ClkValueEnumUpgrader& operator=(ClkValueEnumUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Source type FQN
  std::string src_fqn_;

  /// Map from source enum value to destination enum value
  std::unordered_map<SrcValueType, DestValueType> value_map_;
};

/// Initializer for a clockwork schema
class ClkSchemaInitializer : public ClkValueInitializer
{
public:
  /// Constructor
  /// @param[in] initializers Field initializers
  explicit ClkSchemaInitializer(std::vector<ClkFieldInitializer> initializers);

  ~ClkSchemaInitializer() override = default;

  ClkSchemaInitializer(const ClkSchemaInitializer&) = delete;
  ClkSchemaInitializer& operator=(const ClkSchemaInitializer&) = delete;
  ClkSchemaInitializer(ClkSchemaInitializer&&) = delete;
  ClkSchemaInitializer& operator=(ClkSchemaInitializer&&) = delete;

  /// @see ClkValueInitializer::initialize
  void initialize(std::span<std::byte> dest_span) const override;

private:
  /// Field initializers
  std::vector<ClkFieldInitializer> initializers_;
};

ClkSchemaInitializer::ClkSchemaInitializer(std::vector<ClkFieldInitializer> initializers)
  : initializers_(std::move(initializers))
{
}

void ClkSchemaInitializer::initialize(std::span<std::byte> dest_span) const
{
  for (const auto& initializer : initializers_)
  {
    initializer.initialize(dest_span);
  }
}

/// Clockwork schema type
class ClkSchemaType : public ClkType
{
public:
  /// Constructor, use from_proto to create an instance
  /// @param[in] fqn Fully qualified name
  /// @param[in] type_id Clockwork type ID
  /// @param[in] type_index Clockwork type index
  /// @param[in] size Type size in bytes
  /// @param[in] alignment Type alignment in bytes
  /// @param[in] version Schema version
  /// @param[in] uuid Schema uuid
  ClkSchemaType(
    std::string_view fqn,
    ClkTypeId type_id,
    size_t type_index,
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
  [[nodiscard]] static std::unique_ptr<ClkSchemaType> from_proto(
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

  /// @return Map from field number to field definition
  [[nodiscard]] const std::unordered_map<int32_t, std::unique_ptr<ClkField>>& get_fields() const noexcept;

  /// @return Set of fields that have been removed from the current schema
  [[nodiscard]] const std::unordered_set<int32_t>& get_removed() const noexcept;

  /// @return Map from old to new field numbers for fields modified in the current schema
  [[nodiscard]] const std::unordered_map<int32_t, int32_t>& get_became() const noexcept;

  /// @return Set of historical schema versions
  [[nodiscard]] const std::unordered_set<int32_t>& get_versions() const noexcept;

  /// @return Set of historical schema versions
  [[nodiscard]] std::unordered_set<int32_t>& get_versions() noexcept;

  /// @see ClkType::make_initializer
  [[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> make_initializer() override;

  /// @see ClkType::make_upgrader
  [[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> make_upgrader(const ClkType& src_type) override;

  /// @see ClkType::use_memcpy_for_array_upgrade
  [[nodiscard]] bool use_memcpy_for_array_upgrade(const ClkType& src_type) override;

  /// @see ClkType::is_legacy_wire_compatible
  [[nodiscard]] bool is_legacy_wire_compatible(const ClkType& src_type) const override;

  /// @see ClkType::check_for_unexpected_schema_changes
  void check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view name) override;

private:
  /// Type size in bytes
  size_t size_;

  /// Type alignment in bytes
  size_t alignment_;

  /// Schema version
  int32_t version_;

  /// Schema UUID
  SchemaUuid uuid_;

  /// Map from field number to field definition
  std::unordered_map<int32_t, std::unique_ptr<ClkField>> fields_;

  /// Set of fields that have been removed from the current schema
  std::unordered_set<int32_t> removed_;

  /// Map from old to new field numbers for fields modified in the current schema
  std::unordered_map<int32_t, int32_t> became_;

  /// Historical schema versions
  std::unordered_set<int32_t> versions_;

  /// Cached schema initializer
  std::optional<ClkSchemaInitializer> maybe_initializer_;

  /// Flag set when the cached initializer is value
  bool cached_initializer_valid_{false};

  /// Cached results of checks whether to use memcpy for upgrade
  std::unordered_map<size_t, bool> use_memcpy_cache_;

  /// Cache of checks for unexpected schema changes
  std::unordered_set<size_t> unexpected_schema_changes_cache_;
};

ClkSchemaType::ClkSchemaType(
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  size_t size,
  size_t alignment,
  int32_t version,
  const SchemaUuid& uuid)
  : ClkType(fqn, type_id, type_index), size_(size), alignment_(alignment), version_(version), uuid_(uuid)
{
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::unique_ptr<ClkSchemaType> ClkSchemaType::from_proto(
  jewels::memory::ObjectPtr<ClkTypeFactory> factory,
  const metadata::SchemaType& schema_proto,
  size_t type_index,
  std::optional<std::string_view> maybe_strong_type_fqn)
{
  if (schema_proto.schema_uuid().size() != sizeof(SchemaUuid))
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Invalid UUID size: got {} bytes expected {}", schema_proto.schema_uuid().size(), sizeof(SchemaUuid)));
  }
  SchemaUuid uuid{};
  std::memcpy(uuid.uuid.data(), schema_proto.schema_uuid().data(), sizeof(SchemaUuid));
  auto schema = std::make_unique<ClkSchemaType>(
    maybe_strong_type_fqn.value_or(schema_proto.fqn()),
    ClkTypeId::schema,
    type_index,
    static_cast<size_t>(schema_proto.size()),
    static_cast<size_t>(schema_proto.alignment()),
    schema_proto.version(),
    uuid);
  for (const auto& field_proto : schema_proto.fields())
  {
    try
    {
      schema->fields_.emplace(field_proto.num(), clk_field_from_proto(factory, schema_proto.fqn(), field_proto));
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkSchemaFieldUpgradeError(
        fmt::format("Failed to create upgrader for {}.{}: {}", schema_proto.fqn(), field_proto.name(), exc.what()));
    }
  }
  if (schema_proto.has_history())
  {
    for (const auto [from_num, to_num] : schema_proto.history().became())
    {
      schema->became_[from_num] = to_num;
    }
    for (const auto removed_num : schema_proto.history().removed())
    {
      schema->removed_.insert(removed_num);
    }
    for (const auto version_num : schema_proto.history().versions())
    {
      schema->versions_.insert(version_num);
    }
  }
  return schema;
}

[[nodiscard]] size_t ClkSchemaType::get_size() const noexcept
{
  return size_;
}

[[nodiscard]] size_t ClkSchemaType::get_alignment() const noexcept
{
  return alignment_;
}

[[nodiscard]] const SchemaUuid& ClkSchemaType::get_uuid() const noexcept
{
  return uuid_;
}

[[nodiscard]] int32_t ClkSchemaType::get_version() const noexcept
{
  return version_;
}

[[nodiscard]] const std::unordered_map<int32_t, std::unique_ptr<ClkField>>& ClkSchemaType::get_fields() const noexcept
{
  return fields_;
}

[[nodiscard]] const std::unordered_set<int32_t>& ClkSchemaType::get_removed() const noexcept
{
  return removed_;
}

[[nodiscard]] const std::unordered_map<int32_t, int32_t>& ClkSchemaType::get_became() const noexcept
{
  return became_;
}

[[nodiscard]] const std::unordered_set<int32_t>& ClkSchemaType::get_versions() const noexcept
{
  return versions_;
}

[[nodiscard]] std::unordered_set<int32_t>& ClkSchemaType::get_versions() noexcept
{
  return versions_;
}

[[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> ClkSchemaType::make_initializer()
{
  if (!cached_initializer_valid_)
  {
    std::vector<ClkFieldInitializer> initializers;
    initializers.reserve(fields_.size());
    for (const auto& field : std::ranges::views::values(fields_))
    {
      if (auto maybe_initializer = field->make_initializer(); maybe_initializer)
      {
        initializers.emplace_back(*maybe_initializer);
      }
    }
    if (!initializers.empty())
    {
      initializers.shrink_to_fit();
      maybe_initializer_.emplace(std::move(initializers));
    }
    cached_initializer_valid_ = true;
  }
  if (maybe_initializer_)
  {
    return jewels::memory::make_non_null_from_ref(*maybe_initializer_);
  }
  return std::nullopt;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> ClkSchemaType::make_upgrader(const ClkType& src_type)
{
  auto& upgrader_cache = get_upgrader_cache();
  if (const auto cache_iter = upgrader_cache.find(src_type.get_type_index()); cache_iter != upgrader_cache.end())
  {
    return jewels::memory::make_non_null_from_ref(*cache_iter->second);
  }
  if (use_memcpy_for_array_upgrade(src_type))
  {
    return jewels::memory::make_non_null_from_ref(
      *upgrader_cache.emplace(src_type.get_type_index(), std::make_shared<ClkMemcpyUpgrader>(get_size()))
         .first->second);
  }
  if (src_type.get_type_id() != ClkTypeId::schema)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from non-schema type {} ({}) to {}", src_type.get_fqn(), src_type.get_type_id(), get_fqn()));
  }
  const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_type);
  if (src_schema.get_uuid() != uuid_)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with UUID {} to {} with UUID {}",
        src_type.get_fqn(),
        src_schema.get_uuid(),
        get_fqn(),
        get_uuid()));
  }
  if (src_schema.get_version() > version_)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with version {} to {} with version {}",
        src_schema.get_fqn(),
        src_schema.get_version(),
        get_fqn(),
        version_));
  }
  if (src_schema.get_version() != version_ && !versions_.contains(src_schema.get_version()))
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} to {}, version {} not in schema version history",
        src_schema.get_fqn(),
        get_fqn(),
        src_schema.get_version()));
  }
  std::unordered_set<int32_t> upgraded_fields;
  std::vector<ClkFieldUpgrader> upgraders;
  upgraders.reserve(fields_.size());
  for (const auto& src_field : std::ranges::views::values(src_schema.get_fields()))
  {
    int32_t dest_field_num = src_field->get_num();
    while (became_.contains(dest_field_num))
    {
      dest_field_num = became_.at(dest_field_num);
    }
    if (removed_.contains(dest_field_num))
    {
      continue;
    }
    if (!fields_.contains(dest_field_num))
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Field {} ({}) removed from {} without updating history",
          src_field->get_num(),
          src_field->get_name(),
          get_fqn()));
    }
    const auto& dest_field = fields_.at(dest_field_num);
    try
    {
      upgraders.emplace_back(dest_field->make_upgrader((*src_field)));
    }
    catch (const ClkTypeUpgradeError& exc)
    {
      throw ClkSchemaFieldUpgradeError(
        fmt::format("Failed to create upgrader for {}.{}: {}", get_fqn(), dest_field->get_name(), exc.what()));
    }
    upgraded_fields.emplace(dest_field_num);
  }
  std::vector<ClkFieldInitializer> initializers;
  initializers.reserve(fields_.size() - upgraders.size());
  for (const auto& field : std::ranges::views::values(fields_))
  {
    if (!upgraded_fields.contains(field->get_num()))
    {
      if (auto maybe_initializer = field->make_initializer(); maybe_initializer)
      {
        initializers.emplace_back(*std::move(maybe_initializer));
      }
    }
  }
  upgraders.shrink_to_fit();
  initializers.shrink_to_fit();
  return jewels::memory::make_non_null_from_ref(
    *upgrader_cache
       .emplace(
         src_schema.get_type_index(),
         jewels::memory::make_shared<ClkSchemaUpgrader>(std::move(initializers), std::move(upgraders)))
       .first->second);
}

[[nodiscard]] bool ClkSchemaType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (const auto cache_iter = use_memcpy_cache_.find(src_type.get_type_index()); cache_iter != use_memcpy_cache_.end())
  {
    return cache_iter->second;
  }
  if (src_type.get_type_id() != ClkTypeId::schema)
  {
    use_memcpy_cache_.emplace(src_type.get_type_index(), false);
    return false;
  }
  const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_type);
  if (
    src_schema.get_uuid() != get_uuid() || src_schema.get_version() != get_version() ||
    src_schema.get_fields().size() != fields_.size())
  {
    use_memcpy_cache_.emplace(src_type.get_type_index(), false);
    return false;
  }
  for (const auto& src_field : std::ranges::views::values(src_schema.get_fields()))
  {
    const auto fields_iter = fields_.find(src_field->get_num());
    if (fields_iter == fields_.end())
    {
      use_memcpy_cache_.emplace(src_type.get_type_index(), false);
      return false;
    }
    const auto& src_field_type = src_field->get_field_type();
    const auto& dest_field = fields_iter->second;
    auto& dest_field_type = dest_field->get_field_type();
    if (
      src_field->get_offset() != dest_field->get_offset() ||
      !dest_field_type.use_memcpy_for_array_upgrade(src_field_type))
    {
      use_memcpy_cache_.emplace(src_type.get_type_index(), false);
      return false;
    }
  }
  use_memcpy_cache_.emplace(src_type.get_type_index(), true);
  return true;
}

[[nodiscard]] bool ClkSchemaType::is_legacy_wire_compatible(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::schema)
  {
    return false;
  }
  const auto& src_schema_type = dynamic_cast<const ClkSchemaType&>(src_type);
  if (
    src_schema_type.get_size() != get_size() || src_schema_type.get_alignment() != get_alignment() ||
    src_schema_type.uuid_ != uuid_ || src_schema_type.version_ != version_ ||
    src_schema_type.fields_.size() != fields_.size())
  {
    return false;
  }
  size_t matched_field_count = 0U;
  for (const auto& [field_num, field] : fields_)
  {
    if (!src_schema_type.fields_.contains(field_num))
    {
      return false;
    }
    const auto& src_field = src_schema_type.fields_.at(field_num);
    if (!field->is_legacy_wire_compatible(*src_field))
    {
      return false;
    }
    ++matched_field_count;
  }
  return matched_field_count == fields_.size();
}

void ClkSchemaType::check_for_unexpected_schema_changes(const ClkType& src_type, std::string_view /*name*/)
{
  if (const auto inserted = unexpected_schema_changes_cache_.insert(src_type.get_type_index()).second; !inserted)
  {
    return;
  }
  if (src_type.get_type_id() != ClkTypeId::schema)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from non-schema type {} ({}) to {}", src_type.get_fqn(), src_type.get_type_id(), get_fqn()));
  }
  const auto& src_schema = dynamic_cast<const ClkSchemaType&>(src_type);
  if (src_schema.get_uuid() != uuid_)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with UUID {} to {} with UUID {}",
        src_type.get_fqn(),
        src_schema.get_uuid(),
        get_fqn(),
        get_uuid()));
  }
  if (src_schema.get_version() > version_)
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} with version {} to {} with version {}",
        src_schema.get_fqn(),
        src_schema.get_version(),
        get_fqn(),
        version_));
  }
  if (src_schema.get_version() != version_ && !versions_.contains(src_schema.get_version()))
  {
    throw ClkTypeUpgradeError(
      fmt::format(
        "Cannot upgrade from {} to {}, version {} not in schema version history",
        src_schema.get_fqn(),
        get_fqn(),
        src_schema.get_version()));
  }
  const auto src_fields_view = std::ranges::views::keys(src_schema.get_fields());
  std::unordered_set<int32_t> added_fields{src_fields_view.begin(), src_fields_view.end()};
  for (const auto& src_field : std::ranges::views::values(src_schema.get_fields()))
  {
    int32_t dest_field_num = src_field->get_num();
    while (became_.contains(dest_field_num))
    {
      dest_field_num = became_.at(dest_field_num);
      if (src_schema.get_version() == version_)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Field {} ({}) modified without changing schema version",
            src_field->get_num(),
            src_field->get_name(),
            get_fqn()));
      }
    }
    if (removed_.contains(dest_field_num))
    {
      if (src_schema.get_version() == version_)
      {
        throw ClkTypeUpgradeError(
          fmt::format(
            "Field {} ({}) removed from {} without changing schema version",
            src_field->get_num(),
            src_field->get_name(),
            get_fqn()));
      }
      continue;
    }
    if (!fields_.contains(dest_field_num))
    {
      throw ClkTypeUpgradeError(
        fmt::format(
          "Field {} ({}) removed from {} without updating history",
          src_field->get_num(),
          src_field->get_name(),
          get_fqn()));
    }
    added_fields.erase(dest_field_num);
    const auto& dest_field = fields_.at(dest_field_num);
    dest_field->check_for_unexpected_schema_changes(*src_field, get_fqn());
  }
  if (!added_fields.empty() && src_schema.get_version() == version_)
  {
    const auto& dest_field = fields_.at(*added_fields.begin());
    throw ClkTypeUpgradeError(
      fmt::format(
        "Field {} ({}) added to {} without changing schema version",
        dest_field->get_num(),
        dest_field->get_name(),
        get_fqn()));
  }
}

} // namespace

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
