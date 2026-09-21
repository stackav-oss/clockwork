// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/clk_schema_type_lib.hh"

#include "clockwork/serialization/cpp/metadata_versions.hh"
#include "jewels/memory/pmr_shared_ptr.hh"
#include "jewels/uuid/uuid.hh"

#include <fmt/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <ranges>
#include <utility>

namespace clockwork::serialization
{

using jewels::failure;
using jewels::InOut;
using jewels::ok;
using jewels::success;

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

ClkFieldInitializer::ClkFieldInitializer(
  size_t field_offset, size_t field_size, jewels::memory::ObjectPtr<const ClkValueInitializer> field_initializer)
  : field_offset_(field_offset), field_size_(field_size), field_initializer_(field_initializer)
{
}

void ClkFieldInitializer::initialize(std::span<std::byte> dest_span) const
{
  field_initializer_->initialize(dest_span.subspan(field_offset_, field_size_));
}

ClkFieldLiteCompressor::ClkFieldLiteCompressor(
  size_t offset, size_t size, jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> compressor)
  : offset_(offset), size_(size), compressor_(std::move(compressor))
{
}

jewels::BinaryOutcome ClkFieldLiteCompressor::compress(
  std::span<const std::byte> schema_data, size_t schema_offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  return compressor_->compress(schema_data.subspan(offset_, size_), schema_offset + offset_, InOut{*zero_chunks});
}

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

[[nodiscard]] jewels::BinaryOutcome ClkField::get_value_initializer(
  jewels::FactoryOut<jewels::memory::ObjectPtr<const ClkValueInitializer>> /* initializer_out */) const
{
  return jewels::failure;
}

[[nodiscard]] bool ClkField::is_legacy_wire_compatible(const ClkField& src_field) const
{
  return src_field.num_ == num_ && src_field.name_ == name_ && src_field.offset_ == offset_ &&
         src_field.type_index_ == type_index_;
}

void ClkField::check_for_unexpected_schema_changes(ClkField& src_field, bool allow_changes, std::string_view name)
{
  const auto field_name = fmt::format("{}.{}", name, name_);
  if (!allow_changes)
  {
    if (src_field.name_ != name_)
    {
      throw ClkTypeUpgradeError(
        fmt::format("Field {} renamed to {} in {} without changing field number", src_field.name_, name_, name));
    }
  }
  get_field_type().check_for_unexpected_schema_changes(src_field.get_field_type(), allow_changes, field_name);
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

[[nodiscard]] ClkFieldLiteCompressor ClkField::make_lite_compressor()
{
  return {get_offset(), get_field_type().get_size(), get_field_type().make_lite_compressor()};
}

[[nodiscard]] bool ClkField::is_compressible()
{
  return get_field_type().is_compressible();
}

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

[[nodiscard]] jewels::BinaryOutcome ClkInitialValueField::get_value_initializer(
  jewels::FactoryOut<jewels::memory::ObjectPtr<const ClkValueInitializer>> initializer_out) const
{
  if (initializer_)
  {
    initializer_out->emplace(jewels::memory::make_non_null_from_ref(*initializer_));
    return jewels::success;
  }
  return jewels::failure;
}

namespace
{

/// Construct a clockwork schema field from a protobuf
/// @param[in] factory Clockwork type factory
/// @param[in] schema_fqn Fully qualified name of the schema that contains the field
/// @param[in] field_proto Tachyon schema field protobuf
/// @return Clockwork schema field instance
// Recursion needed because types are defined recursively.
// Functional complexity is due to the switch statement that needs to handle all of the field types
// NOLINTNEXTLINE(misc-no-recursion, readability-function-cognitive-complexity) See above
std::shared_ptr<ClkField> clk_field_from_proto(
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
    return jewels::memory::make_pmr_shared<ClkInitialValueField>(
      factory->get_memory_resource(),
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
  case ClkTypeId::fixed_soa:
  case ClkTypeId::var_soa:
  case ClkTypeId::schema:
  case ClkTypeId::tensor:
  case ClkTypeId::bitset:
    return jewels::memory::make_pmr_shared<ClkField>(
      factory->get_memory_resource(),
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
  ClkSchemaUpgrader(std::pmr::vector<ClkFieldInitializer> initializers, std::pmr::vector<ClkFieldUpgrader> upgraders);

  ~ClkSchemaUpgrader() noexcept override = default;

  ClkSchemaUpgrader(const ClkSchemaUpgrader&) = delete;
  ClkSchemaUpgrader& operator=(const ClkSchemaUpgrader&) = delete;
  ClkSchemaUpgrader(ClkSchemaUpgrader&&) = delete;
  ClkSchemaUpgrader& operator=(ClkSchemaUpgrader&&) = delete;

  /// @see ClkTypeUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Field initializers
  std::pmr::vector<ClkFieldInitializer> initializers_;

  /// Field upgraders
  std::pmr::vector<ClkFieldUpgrader> upgraders_;
};

ClkSchemaUpgrader::ClkSchemaUpgrader(
  std::pmr::vector<ClkFieldInitializer> initializers, std::pmr::vector<ClkFieldUpgrader> upgraders)
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

} // namespace

ClkSchemaInitializer::ClkSchemaInitializer(std::pmr::vector<ClkFieldInitializer> initializers)
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

// NOLINTNEXTLINE(readability-function-size) Parameters are only used for private constructor.
ClkSchemaType::ClkSchemaType(
  jewels::memory::MemoryResource memory_resource,
  std::string_view fqn,
  ClkTypeId type_id,
  size_t type_index,
  int32_t metadata_version,
  size_t size,
  size_t alignment,
  int32_t version,
  const SchemaUuid& uuid)
  : ClkType(memory_resource, fqn, type_id, type_index, metadata_version),
    size_(size),
    alignment_(alignment),
    version_(version),
    uuid_(uuid),
    arguments_(memory_resource),
    fields_(memory_resource),
    removed_(memory_resource),
    became_(memory_resource),
    use_memcpy_cache_(memory_resource),
    unexpected_schema_changes_cache_(memory_resource)
{
}

/// Lite compressor for schema types
class ClkSchemaLiteCompressor : public ClkTypeLiteCompressor
{
public:
  /// Constructor
  /// @param[in] size Schema size
  /// @param[in] compressors Schema field compressors
  ClkSchemaLiteCompressor(size_t size, std::pmr::vector<ClkFieldLiteCompressor> compressors);

  ~ClkSchemaLiteCompressor() noexcept override = default;

  ClkSchemaLiteCompressor(const ClkSchemaLiteCompressor&) = delete;
  ClkSchemaLiteCompressor& operator=(const ClkSchemaLiteCompressor&) = delete;
  ClkSchemaLiteCompressor(ClkSchemaLiteCompressor&&) = delete;
  ClkSchemaLiteCompressor& operator=(ClkSchemaLiteCompressor&&) = delete;

  /// @see ClkTypeLiteCompressor::compress
  jewels::BinaryOutcome compress(
    std::span<const std::byte> data_span,
    size_t offset,
    InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const override;

private:
  /// Schema size in bytes
  size_t size_;

  /// Field compressors
  std::pmr::vector<ClkFieldLiteCompressor> compressors_;
};

ClkSchemaLiteCompressor::ClkSchemaLiteCompressor(size_t size, std::pmr::vector<ClkFieldLiteCompressor> compressors)
  : size_(size), compressors_(std::move(compressors))
{
}

jewels::BinaryOutcome ClkSchemaLiteCompressor::compress(
  std::span<const std::byte> data_span, size_t offset, InOut<std::pmr::vector<ClkZeroChunk>> zero_chunks) const
{
  if (data_span.size() != size_)
  {
    return failure;
  }
  if (compressors_.empty())
  {
    ClkTypeLiteCompressor::compress_opaque_data(data_span, offset, InOut{*zero_chunks});
  }
  else
  {
    for (const auto& compressor : compressors_)
    {
      if (!ok(compressor.compress(data_span, offset, InOut{*zero_chunks})))
      {
        return failure;
      }
    }
  }
  return success;
}

// NOLINTNEXTLINE(misc-no-recursion) Types are defined recursively
[[nodiscard]] std::shared_ptr<ClkSchemaType> ClkSchemaType::from_proto(
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
  auto schema = jewels::memory::make_pmr_shared<ClkSchemaType>(
    factory->get_memory_resource(),
    factory->get_memory_resource(),
    maybe_strong_type_fqn.value_or(schema_proto.fqn()),
    ClkTypeId::schema,
    type_index,
    factory->get_metadata_version(),
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
  }
  for (const auto& argument : schema_proto.arguments())
  {
    if (argument.has_type_id())
    {
      schema->arguments_.emplace_back(factory->get_clk_type(static_cast<size_t>(argument.type_id())));
    }
    else
    {
      schema->arguments_.emplace_back(std::pmr::string{argument.value(), factory->get_memory_resource()});
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

[[nodiscard]] const std::pmr::vector<ClkSchemaArgumentType>& ClkSchemaType::get_arguments() const noexcept
{
  return arguments_;
}

[[nodiscard]] const std::pmr::unordered_map<int32_t, std::shared_ptr<ClkField>>&
ClkSchemaType::get_fields() const noexcept
{
  return fields_;
}

[[nodiscard]] const std::pmr::set<int32_t>& ClkSchemaType::get_removed() const noexcept
{
  return removed_;
}

[[nodiscard]] const std::pmr::map<int32_t, int32_t>& ClkSchemaType::get_became() const noexcept
{
  return became_;
}

[[nodiscard]] std::optional<jewels::memory::ObjectPtr<const ClkValueInitializer>> ClkSchemaType::make_initializer()
{
  if (!cached_initializer_valid_)
  {
    std::pmr::vector<ClkFieldInitializer> initializers(get_memory_resource());
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
      *upgrader_cache
         .emplace(
           src_type.get_type_index(),
           jewels::memory::make_pmr_shared<ClkMemcpyUpgrader>(get_memory_resource(), get_size()))
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
  std::pmr::unordered_set<int32_t> upgraded_fields(get_memory_resource());
  std::pmr::vector<ClkFieldUpgrader> upgraders(get_memory_resource());
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
  std::pmr::vector<ClkFieldInitializer> initializers(get_memory_resource());
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
         jewels::memory::make_pmr_shared<ClkSchemaUpgrader>(
           get_memory_resource(), std::move(initializers), std::move(upgraders)))
       .first->second);
}

[[nodiscard]] bool ClkSchemaType::use_memcpy_for_array_upgrade(const ClkType& src_type)
{
  if (const auto cache_iter = use_memcpy_cache_.find(src_type.get_type_index()); cache_iter != use_memcpy_cache_.end())
  {
    return cache_iter->second;
  }
  if (!is_same_type(src_type))
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
  if (!is_same_type(src_type))
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

void ClkSchemaType::check_for_unexpected_schema_changes(
  ClkType& src_type, bool /*allow_changes*/, std::string_view /*name*/)
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
  const auto fields_view = std::ranges::views::keys(fields_);
  std::pmr::unordered_set<int32_t> added_fields{get_memory_resource()};
  added_fields.insert(fields_view.begin(), fields_view.end());
  // Allow changes to the field types if the version has changed or the source schema is a different type
  // due to a parameter change
  const auto allow_changes = !is_same_type(src_type) || src_schema.get_version() != version_;
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
    dest_field->check_for_unexpected_schema_changes(*src_field, allow_changes, get_fqn());
  }
  if (
    !added_fields.empty() && src_schema.get_version() == version_ &&
    get_metadata_version() >= enforce_version_change_when_adding_fields_and_values_version)
  {
    const auto& dest_field = fields_.at(*added_fields.begin());
    throw ClkTypeUpgradeError(
      fmt::format(
        "Field {} ({}) added to {} without changing schema version",
        dest_field->get_num(),
        dest_field->get_name(),
        get_fqn()));
  }
  check_for_unexpected_history_changes(
    get_memory_resource(),
    src_schema.get_became(),
    src_schema.get_removed(),
    became_,
    removed_,
    allow_changes,
    get_fqn());
}

[[nodiscard]] bool ClkSchemaType::is_same_type(const ClkType& src_type) const
{
  if (src_type.get_type_id() != ClkTypeId::schema)
  {
    return false;
  }
  const auto& src_schema_type = dynamic_cast<const ClkSchemaType&>(src_type);
  if (src_schema_type.get_uuid() != get_uuid())
  {
    return false;
  }
  const auto& src_arguments = src_schema_type.get_arguments();
  if (src_arguments.size() != arguments_.size())
  {
    return false;
  }
  for (size_t arg_index = 0U; arg_index < arguments_.size(); ++arg_index)
  {
    const auto& argument = arguments_.at(arg_index);
    const auto& src_argument = src_arguments.at(arg_index);
    if (std::holds_alternative<std::pmr::string>(argument))
    {
      if (!std::holds_alternative<std::pmr::string>(src_argument))
      {
        return false;
      }
      if (std::get<std::pmr::string>(argument) != std::get<std::pmr::string>(src_argument))
      {
        return false;
      }
    }
    else
    {
      if (!std::holds_alternative<jewels::memory::ObjectPtr<ClkType>>(src_argument))
      {
        return false;
      }
      if (!std::get<jewels::memory::ObjectPtr<ClkType>>(argument)->is_same_type(
            *std::get<jewels::memory::ObjectPtr<ClkType>>(src_argument)))
      {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor> ClkSchemaType::make_lite_compressor()
{
  auto& cached_lite_compressor = get_cached_lite_compressor();
  if (!cached_lite_compressor)
  {
    std::pmr::vector<ClkFieldLiteCompressor> compressors(get_memory_resource());
    const auto& fields = get_fields();
    compressors.reserve(fields.size());
    for (const auto& field : std::ranges::views::values(fields))
    {
      if (field->is_compressible())
      {
        compressors.emplace_back(field->make_lite_compressor());
      }
    }
    std::sort(compressors.begin(), compressors.end());
    cached_lite_compressor = jewels::memory::make_pmr_shared<ClkSchemaLiteCompressor>(
      get_memory_resource(), get_size(), std::move(compressors));
  }
  return jewels::memory::NonNullSharedPtr<ClkTypeLiteCompressor>{cached_lite_compressor};
}

[[nodiscard]] bool ClkSchemaType::is_compressible()
{
  auto& cached_is_compressible = get_cached_is_compressible();
  if (!cached_is_compressible.has_value())
  {
    cached_is_compressible = true;
  }
  return cached_is_compressible.value();
}

} // namespace clockwork::serialization
