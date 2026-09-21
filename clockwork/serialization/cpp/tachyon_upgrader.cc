// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_upgrader.hh"

#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace clockwork::serialization
{

namespace
{

/// Class to upgrade a tachyon type from a the source schema to the destination schema using a generated upgrader
class TachyonCppUpgrader : public TachyonUpgrader
{
public:
  /// Constructor
  /// @param[in] upgrader Upgrade function
  /// @param[in] src_model Source tachyon model
  /// @param[in] dest_model Destination tachyon model
  TachyonCppUpgrader(
    jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader,
    std::shared_ptr<TachyonModel> src_model,
    std::shared_ptr<TachyonModel> dest_model);

  ~TachyonCppUpgrader() noexcept override = default;

  TachyonCppUpgrader(const TachyonCppUpgrader&) = delete;
  TachyonCppUpgrader& operator=(const TachyonCppUpgrader&) = delete;
  TachyonCppUpgrader(TachyonCppUpgrader&&) noexcept = default;
  TachyonCppUpgrader& operator=(TachyonCppUpgrader&&) noexcept = default;

  /// @see TachyonUpgrader::upgrade_required
  [[nodiscard]] bool upgrade_required() const override;

  /// @see TachyonUprader::upgrader_type
  [[nodiscard]] TachyonUpgraderType upgrader_type() const override;

  /// @see TachyonUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Schema upgrader
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader_;

  /// Source tachyon model
  std::shared_ptr<TachyonModel> src_model_;

  /// Destination tachyon model
  std::shared_ptr<TachyonModel> dest_model_;

  /// Source schema size
  size_t src_size_;

  /// Source schema FQN
  std::string src_fqn_;

  /// Destination schema size
  size_t dest_size_;

  /// Destination schema FQN
  std::string dest_fqn_;
};

TachyonCppUpgrader::TachyonCppUpgrader(
  jewels::memory::ObjectPtr<const ClkTypeUpgrader> upgrader,
  std::shared_ptr<TachyonModel> src_model,
  std::shared_ptr<TachyonModel> dest_model)
  : upgrader_(upgrader),
    src_model_(std::move(src_model)),
    dest_model_(std::move(dest_model)),
    src_size_(src_model_->get_outer_type().get_size()),
    src_fqn_(src_model_->get_outer_type().get_fqn()),
    dest_size_(dest_model_->get_outer_type().get_size()),
    dest_fqn_(dest_model_->get_outer_type().get_fqn())
{
}

void TachyonCppUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if (src_span.size() != src_size_)
  {
    throw std::runtime_error(
      fmt::format(
        "Cannot upgrade {}: invalid source schema size, got {} expected {}", src_fqn_, src_span.size(), src_size_));
  }
  if (dest_span.size() != dest_size_)
  {
    throw std::runtime_error(
      fmt::format(
        "Cannot upgrade {}: invalid destination schema size, got {} expected {}",
        dest_fqn_,
        dest_span.size(),
        dest_size_));
  }
  upgrader_->upgrade(src_span, dest_span);
}

[[nodiscard]] bool TachyonCppUpgrader::upgrade_required() const
{
  return true;
}

[[nodiscard]] TachyonUpgraderType TachyonCppUpgrader::upgrader_type() const
{
  return TachyonUpgraderType::cpp;
}

/// Class to upgrade a tachyon type from a the source schema to the destination when the types are wire compatible
class TachyonMemcpyUpgrader : public TachyonUpgrader
{
public:
  /// Constructor
  /// @param[in] schema_size Schema size
  /// @param[in] src_fqn Source schema FQN
  /// @param[in] dest_fqn Destination schema FQN
  TachyonMemcpyUpgrader(size_t schema_size, std::string_view src_fqn, std::string_view dest_fqn);

  ~TachyonMemcpyUpgrader() noexcept override = default;

  TachyonMemcpyUpgrader(const TachyonMemcpyUpgrader&) = delete;
  TachyonMemcpyUpgrader& operator=(const TachyonMemcpyUpgrader&) = delete;
  TachyonMemcpyUpgrader(TachyonMemcpyUpgrader&&) noexcept = default;
  TachyonMemcpyUpgrader& operator=(TachyonMemcpyUpgrader&&) noexcept = default;

  /// @see TachyonUpgrader::upgrade_required
  [[nodiscard]] bool upgrade_required() const override;

  /// @see TachyonUprader::upgrader_type
  [[nodiscard]] TachyonUpgraderType upgrader_type() const override;

  /// @see TachyonUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Schema size
  size_t schema_size_;

  /// Source schema FQN
  std::string src_fqn_;

  /// Destination schema FQN
  std::string dest_fqn_;
};

TachyonMemcpyUpgrader::TachyonMemcpyUpgrader(size_t schema_size, std::string_view src_fqn, std::string_view dest_fqn)
  : schema_size_(schema_size), src_fqn_(src_fqn), dest_fqn_(dest_fqn)
{
}

void TachyonMemcpyUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  if (src_span.size() != schema_size_)
  {
    throw std::runtime_error(
      fmt::format(
        "Cannot upgrade {}: invalid source schema size, got {} expected {}", src_fqn_, src_span.size(), schema_size_));
  }
  if (dest_span.size() != schema_size_)
  {
    throw std::runtime_error(
      fmt::format(
        "Cannot upgrade {}: invalid destination schema size, got {} expected {}",
        dest_fqn_,
        dest_span.size(),
        schema_size_));
  }
  std::ranges::copy(src_span.begin(), src_span.end(), dest_span.begin());
}

[[nodiscard]] bool TachyonMemcpyUpgrader::upgrade_required() const
{
  return false;
}

[[nodiscard]] TachyonUpgraderType TachyonMemcpyUpgrader::upgrader_type() const
{
  return TachyonUpgraderType::memcpy;
}

} // namespace

[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_cpp_upgrader(
  std::string_view current_class_name,
  std::span<const std::byte> current_metadata,
  std::span<const std::byte> incoming_metadata)
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  const std::string current_metadata_str{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting serialized metadata from bytes to string
    reinterpret_cast<const char*>(current_metadata.data()),
    current_metadata.size()};
  auto current_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!current_proto->ParseFromString(current_metadata_str))
  {
    throw std::runtime_error("Failed to parse destination metadata");
  }
  if (current_proto->python_required())
  {
    throw std::runtime_error(
      fmt::format(
        "Cannot make C++ upgrader for {}, current schema has python_required set to true", current_class_name));
  }
  auto current_model = TachyonModel::from_proto(memory_resource, std::move(current_proto));
  const std::string incoming_metadata_str{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting serialized metadata from bytes to string
    reinterpret_cast<const char*>(incoming_metadata.data()),
    incoming_metadata.size()};
  auto incoming_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!incoming_proto->ParseFromString(incoming_metadata_str))
  {
    throw std::runtime_error("Failed to parse source metadata");
  }
  auto incoming_model = TachyonModel::from_proto(memory_resource, std::move(incoming_proto));
  if (current_model->is_wire_compatible(*incoming_model))
  {
    return make_tachyon_memcpy_upgrader(
      current_model->get_outer_type().get_size(),
      incoming_model->get_outer_type().get_fqn(),
      current_model->get_outer_type().get_fqn());
  }
  const auto upgrader = current_model->make_upgrader(*incoming_model);
  return std::make_unique<TachyonCppUpgrader>(upgrader, std::move(incoming_model), std::move(current_model));
}

[[nodiscard]] std::unique_ptr<TachyonUpgrader>
make_tachyon_memcpy_upgrader(size_t schema_size, std::string_view src_fqn, std::string_view dest_fqn)
{
  return std::make_unique<TachyonMemcpyUpgrader>(schema_size, src_fqn, dest_fqn);
}

} // namespace clockwork::serialization
