// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_model.hh"

#include "clockwork/serialization/cpp/clk_builtin_type.hh"
#include "clockwork/serialization/cpp/clk_enum_type.hh"
#include "clockwork/serialization/cpp/clk_schema_type.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"
#include "jewels/hash/md5.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"

#include <fmt10/format.h>
#include <google/protobuf/repeated_ptr_field.h>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace clockwork::serialization
{

namespace
{

using jewels::fails;
using jewels::Out;

/// Initialize a context to compute a hash incrementally
/// @param[out] ctx Context to initialize
/// @throws runtime_error if initialize fails
void md5_initialize(Out<jewels::hash::MD5HashContext> ctx)
{
  if (fails(jewels::hash::md5_initialize(Out{*ctx})))
  {
    throw ClkTypeUpgradeError("MD5 finalize failed");
  }
}

/// Update the computation with a span of bytes
/// @param[in,out] ctx Context to update
/// @param[in] data Data to hash
/// @throws runtime_error if update fails
void md5_update(jewels::hash::MD5HashContext& ctx, std::span<const std::byte> data)
{
  if (fails(jewels::hash::md5_update(ctx, data)))
  {
    throw ClkTypeUpgradeError("MD5 update failed");
  }
}

/// Finalize the hash computation and return the result
/// @param[out] hash Hash value
/// @param[in] ctx Context to finalize
/// @throws runtime_error if finalize fails
void md5_finalize(Out<jewels::hash::MD5HashValue> hash, jewels::hash::MD5HashContext& ctx)
{
  if (fails(jewels::hash::md5_finalize(Out{*hash}, ctx)))
  {
    throw ClkTypeUpgradeError("MD5 finalize failed");
  }
}

/// Compute the type hash for the metadata type at the specified index
/// @param[in] metadata Tachyon metadata proto
/// @param[in,out] hash_cache Type hash cache
/// @param[in] type_index Type index
/// @return Metadata type hash
[[nodiscard]] jewels::hash::MD5HashValue compute_type_hash(
  const metadata::TachyonMetadata& metadata,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache,
  size_t type_index);

/// Compute the type hash for a schema field protobuf
/// @param[in] metadata Tachyon metadata proto
/// @param[in] field_proto Tachyon schema field protobuf
/// @param[in,out] hash_cache Type hash cache
/// @return Schema field hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_field_hash(
  const metadata::TachyonMetadata& metadata,
  const metadata::SchemaField& field_proto,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache)
{
  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  const auto num_str = std::to_string(field_proto.num());
  md5_update(ctx, std::as_bytes(std::span{num_str.data(), num_str.size()}));
  md5_update(ctx, std::as_bytes(std::span{field_proto.name().data(), field_proto.name().size()}));
  const auto arg_hash = compute_type_hash(metadata, Out{*hash_cache}, static_cast<size_t>(field_proto.type_id()));
  md5_update(ctx, std::span{arg_hash});
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for a schema protobuf
/// @param[in] metadata Tachyon metadata proto
/// @param[in] schema_proto Tachyon schema type protobuf
/// @param[in,out] hash_cache Type hash cache
/// @return Schema type hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_schema_hash(
  const metadata::TachyonMetadata& metadata,
  const metadata::SchemaType& schema_proto,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache)
{
  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  md5_update(ctx, std::as_bytes(std::span{schema_proto.schema_uuid().data(), schema_proto.schema_uuid().size()}));
  const auto version_str = std::to_string(schema_proto.version());
  md5_update(ctx, std::as_bytes(std::span{version_str.data(), version_str.size()}));
  for (const auto& field : schema_proto.fields())
  {
    const auto field_hash = compute_field_hash(metadata, field, Out{*hash_cache});
    md5_update(ctx, std::span{field_hash});
  }
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for a builtin type protobuf
/// @param[in] metadata Tachyon metadata proto
/// @param[in] builtin_proto Tachyon built-in type protobuf
/// @param[in,out] hash_cache Type hash cache
/// @return Built-in type hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_built_in_hash(
  const metadata::TachyonMetadata& metadata,
  const metadata::BuiltInType& built_in_proto,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache)
{
  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  md5_update(ctx, std::as_bytes(std::span{built_in_proto.uuid().data(), built_in_proto.uuid().size()}));
  if (built_in_proto.fqn() != ".Uuid")
  {
    for (const auto& arg : built_in_proto.arguments())
    {
      if (arg.has_value())
      {
        md5_update(ctx, std::as_bytes(std::span{arg.value().data(), arg.value().size()}));
      }
      else
      {
        const auto arg_hash = compute_type_hash(metadata, Out{*hash_cache}, static_cast<size_t>(arg.type_id()));
        md5_update(ctx, std::span{arg_hash});
      }
    }
  }
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for an enum value protobuf
/// @param[in] enum_proto Tachyon enum type protobuf
/// @return Enum type hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_enum_value_hash(const metadata::EnumValue& value_proto)
{
  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  const auto num_str = std::to_string(value_proto.num());
  md5_update(ctx, std::as_bytes(std::span{num_str.data(), num_str.size()}));
  md5_update(ctx, std::as_bytes(std::span{value_proto.name().data(), value_proto.name().size()}));
  const auto value_str = std::to_string(value_proto.value());
  md5_update(ctx, std::as_bytes(std::span{value_str.data(), value_str.size()}));
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for an enum type protobuf
/// @param[in] metadata Tachyon metadata proto
/// @param[in] enum_proto Tachyon enum type protobuf
/// @param[in,out] hash_cache Type hash cache
/// @return Enum type hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_enum_hash(
  const metadata::TachyonMetadata& metadata,
  const metadata::ClkEnumType& enum_proto,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache)
{
  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  md5_update(ctx, std::as_bytes(std::span{enum_proto.enum_uuid().data(), enum_proto.enum_uuid().size()}));
  const auto underlying_type_hash =
    compute_type_hash(metadata, Out{*hash_cache}, static_cast<size_t>(enum_proto.underlying_type_id()));
  md5_update(ctx, underlying_type_hash);
  const auto options_str = std::to_string(enum_proto.options());
  md5_update(ctx, std::as_bytes(std::span{options_str.data(), options_str.size()}));
  const auto version_str = std::to_string(enum_proto.version());
  md5_update(ctx, std::as_bytes(std::span{version_str.data(), version_str.size()}));
  for (const auto& val : enum_proto.values())
  {
    const auto val_hash = compute_enum_value_hash(val);
    md5_update(ctx, std::span{val_hash});
  }
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for a tag type protobuf
/// @return Tag type hash
[[nodiscard]] jewels::hash::MD5HashValue compute_tag_hash()
{
  constexpr auto tag_data = std::string_view{"<TAG>"};

  jewels::hash::MD5HashContext ctx{};
  md5_initialize(Out{ctx});
  md5_update(ctx, std::as_bytes(std::span{tag_data.data(), tag_data.size()}));
  jewels::hash::MD5HashValue hash{};
  md5_finalize(Out{hash}, ctx);
  return hash;
}

/// Compute the type hash for a strong type protobuf
/// @param[in] metadata Tachyon metadata proto
/// @param[in] strong_proto Tachyon strong type protobuf
/// @param[in,out] hash_cache Type hash cache
/// @return Strong type hash
// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_strong_hash(
  const metadata::TachyonMetadata& metadata,
  const metadata::StrongType& strong_proto,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache)
{
  return compute_type_hash(metadata, Out{*hash_cache}, static_cast<size_t>(strong_proto.underlying_type_id()));
}

// NOLINTNEXTLINE(misc-no-recursion) Schema is defined recursively
[[nodiscard]] jewels::hash::MD5HashValue compute_type_hash(
  const metadata::TachyonMetadata& metadata,
  Out<std::vector<std::optional<jewels::hash::MD5HashValue>>> hash_cache,
  size_t type_index)
{
  const auto& maybe_hash = hash_cache->at(type_index);
  if (maybe_hash.has_value())
  {
    return maybe_hash.value();
  }
  const auto& type_proto = metadata.types(static_cast<int32_t>(type_index));
  if (type_proto.has_built_in())
  {
    const auto hash = compute_built_in_hash(metadata, type_proto.built_in(), Out{*hash_cache});
    hash_cache->at(type_index) = hash;
    return hash;
  }
  if (type_proto.has_schema())
  {
    const auto hash = compute_schema_hash(metadata, type_proto.schema(), Out{*hash_cache});
    hash_cache->at(type_index) = hash;
    return hash;
  }
  if (type_proto.has_clk_enum())
  {
    const auto hash = compute_enum_hash(metadata, type_proto.clk_enum(), Out{*hash_cache});
    hash_cache->at(type_index) = hash;
    return hash;
  }
  if (type_proto.has_tag())
  {
    const auto hash = compute_tag_hash();
    hash_cache->at(type_index) = hash;
    return hash;
  }
  if (type_proto.has_strong_type())
  {
    const auto hash = compute_strong_hash(metadata, type_proto.strong_type(), Out{*hash_cache});
    hash_cache->at(type_index) = hash;
    return hash;
  }
  throw ClkTypeUpgradeError(fmt::format("Invalid type descriptor at index {}", type_index));
}

} // namespace

TachyonModel::TachyonModel(size_t outer_type_id, std::unique_ptr<ClkTypeFactory> factory)
  : outer_type_id_(outer_type_id), factory_(std::move(factory))
{
}

[[nodiscard]] std::unique_ptr<TachyonModel>
TachyonModel::from_proto(std::unique_ptr<metadata::TachyonMetadata> metadata_proto)
{
  std::vector<std::unique_ptr<ClkTypeFactoryPlugin>> plugins;
  plugins.emplace_back(std::make_unique<ClkBuiltInTypeFactoryPlugin>());
  plugins.emplace_back(std::make_unique<ClkSchemaTypeFactoryPlugin>());
  plugins.emplace_back(std::make_unique<ClkEnumTypeFactoryPlugin>());
  auto factory = std::make_unique<ClkTypeFactory>(std::move(metadata_proto), std::move(plugins));
  auto model = std::make_unique<TachyonModel>(
    static_cast<size_t>(factory->get_metadata_proto().outer_type_id()), std::move(factory));
  for (size_t i = 0U; i < model->factory_->get_types().size(); ++i)
  {
    std::ignore = model->factory_->get_clk_type(i); // Ensure that the types are populated
  }
  return model;
}

[[nodiscard]] std::unique_ptr<TachyonModel> TachyonModel::from_proto(const metadata::TachyonMetadata& metadata_proto)
{
  return from_proto(std::make_unique<metadata::TachyonMetadata>(metadata_proto));
}

[[nodiscard]] const ClkType& TachyonModel::get_outer_type() const
{
  return *factory_->get_types().at(outer_type_id_);
}

[[nodiscard]] ClkType& TachyonModel::get_outer_type()
{
  return *factory_->get_types().at(outer_type_id_);
}

[[nodiscard]] int32_t TachyonModel::get_metadata_version() const
{
  return factory_->get_metadata_proto().version();
}

[[nodiscard]] std::span<const std::byte> TachyonModel::get_metadata_hash() const
{
  const auto& metadata = factory_->get_metadata_proto();
  const auto& outer_type = metadata.types(metadata.outer_type_id());
  if (!outer_type.has_schema())
  {
    throw ClkTypeUpgradeError("Outer type is not a schema");
  }
  return std::as_bytes(std::span{outer_type.schema().hash().data(), outer_type.schema().hash().size()});
}

[[nodiscard]] std::span<const std::byte> TachyonModel::compute_metadata_hash()
{
  if (!maybe_cached_metadata_hash_.has_value())
  {
    const auto& metadata = factory_->get_metadata_proto();
    std::vector<std::optional<jewels::hash::MD5HashValue>> hash_cache(static_cast<size_t>(metadata.types_size()));
    maybe_cached_metadata_hash_ =
      compute_type_hash(metadata, Out{hash_cache}, static_cast<size_t>(metadata.outer_type_id()));
  }
  return std::span{maybe_cached_metadata_hash_.value()};
}

[[nodiscard]] bool TachyonModel::is_wire_compatible(TachyonModel& other)
{
  if (get_outer_type().get_size() != other.get_outer_type().get_size())
  {
    return false;
  }
  if (other.get_metadata_version() == get_metadata_version())
  {
    return std::ranges::equal(get_metadata_hash(), other.get_metadata_hash());
  }
  if (std::min(other.get_metadata_version(), get_metadata_version()) >= built_in_uuid_added_in_version)
  {
    return std::ranges::equal(compute_metadata_hash(), other.compute_metadata_hash());
  }

  if (outer_type_id_ != other.outer_type_id_ || factory_->get_types().size() != other.factory_->get_types().size())
  {
    return false;
  }

  for (size_t type_index = 0U; type_index < factory_->get_types().size(); ++type_index)
  {
    if (!factory_->get_types().at(type_index)->is_legacy_wire_compatible(*other.factory_->get_types().at(type_index)))
    {
      return false;
    }
  }
  return true;
}

[[nodiscard]] jewels::memory::ObjectPtr<const ClkTypeUpgrader> TachyonModel::make_upgrader(TachyonModel& other)
{
  check_for_unexpected_schema_changes(other);
  return get_outer_type().make_upgrader(other.get_outer_type());
}

void TachyonModel::check_for_unexpected_schema_changes(TachyonModel& other)
{
  get_outer_type().check_for_unexpected_schema_changes(other.get_outer_type(), get_outer_type().get_fqn());
}

[[nodiscard]] bool validate_logged_channel_metadata(
  const metadata::LoggedChannelMetadata& prev_metadata, const metadata::LoggedChannelMetadata& curr_metadata)
{
  bool validate_result = true;
  for (const auto& [prev_name, prev_meta] : prev_metadata.channel_metadata())
  {
    if (curr_metadata.channel_metadata().contains(prev_name))
    {
      try
      {
        const auto& curr_meta = curr_metadata.channel_metadata().at(prev_name);
        const auto prev_model = TachyonModel::from_proto(prev_meta);
        const auto curr_model = TachyonModel::from_proto(curr_meta);
        // Ignoring make_upgrader_result, we are just interested in whether this throws an exception
        std::ignore = curr_model->make_upgrader(*prev_model);
      }
      catch (const std::exception& exc)
      {
        jewels::log_cerr_error("{} FAILED: {}", prev_name, exc.what());
        validate_result = false;
      }
    }
  }
  return validate_result;
}

} // namespace clockwork::serialization
