// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/gil_lock_guard.hh"
#include "clockwork/python/python_object.hh"
#include "clockwork/serialization/cpp/clk_type.hh"
#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"

#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace clockwork::serialization
{

namespace
{

/// Class to upgrade a tachyon type to the current version by calling into python
class TachyonPythonUpgrader : public TachyonUpgrader
{
public:
  /// Python module that implemements the schema upgrader
  static constexpr auto create_upgrader_module = "clockwork.serialization.py.create_upgrader";

  /// Python method to create a schema upgrader
  static constexpr auto create_upgrader_method = "create_upgrader";

  /// Constructor, use make_upgrader to make a new instance
  /// @param[in] upgrader Schema upgrader python object
  /// @param[in] current_class_name Current schema class name
  TachyonPythonUpgrader(python::PythonObject upgrader, std::string_view current_class_name);

  /// Destructor releases the upgrader while holding the global interpreter lock
  ~TachyonPythonUpgrader() override;

  TachyonPythonUpgrader(const TachyonPythonUpgrader&) = delete;
  TachyonPythonUpgrader& operator=(const TachyonPythonUpgrader&) = delete;
  TachyonPythonUpgrader(TachyonPythonUpgrader&&) = default;
  TachyonPythonUpgrader& operator=(TachyonPythonUpgrader&&) = default;

  /// Create an instance of a python upgrader.
  /// @param[in] current_module_name Current tachyon schema module name
  /// @param[in] current_source_file_name Current tachyon schema source file name
  /// @param[in] current_class_name Current tachyon schema class name
  /// @param[in] current_metadata Serialized metadata for the current schema
  /// @param[in] incoming_metadata Serialized metadata for the incoming message
  /// @param[in] incoming_schema_name Schema name for the incoming message
  /// @returns Pointer to the schema upgrader or a nullptr if no upgrade is required
  [[nodiscard]] static std::unique_ptr<TachyonUpgrader> make_upgrader(
    std::string_view current_module_name,
    std::string_view current_source_file_name,
    std::string_view current_class_name,
    std::span<const std::byte> current_metadata,
    std::span<const std::byte> incoming_metadata,
    std::string_view incoming_schema_name);

  /// @see TachyonUpgrader::upgrade_required
  [[nodiscard]] bool upgrade_required() const override;

  /// @see TachyonUprader::upgrader_type
  [[nodiscard]] TachyonUpgraderType upgrader_type() const override;

  /// @see TachyonUpgrader::upgrade
  void upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const override;

private:
  /// Schema upgrader python object
  python::PythonObject upgrader_;

  /// Current schema class name
  std::string current_class_name_;
};

TachyonPythonUpgrader::TachyonPythonUpgrader(python::PythonObject upgrader, std::string_view current_class_name)
  : upgrader_(std::move(upgrader)), current_class_name_(current_class_name)
{
}

TachyonPythonUpgrader::~TachyonPythonUpgrader()
{
  if (upgrader_.is_valid())
  {
    const python::GilLockGuard gil_guard;
    upgrader_.reset();
  }
}

[[nodiscard]] std::unique_ptr<TachyonUpgrader> TachyonPythonUpgrader::make_upgrader(
  std::string_view current_module_name,
  std::string_view current_source_file_name,
  std::string_view current_class_name,
  std::span<const std::byte> current_metadata,
  std::span<const std::byte> incoming_metadata,
  std::string_view incoming_schema_name)
{
  const python::GilLockGuard gil_guard;

  const auto create_upgrader_dict = python::PythonObject::import_module(create_upgrader_module);
  const auto create_upgrader_fn = create_upgrader_dict.get_dictionary_item(create_upgrader_method);

  const auto create_result = create_upgrader_fn.call_object(
    python::PythonObject::make_string(current_module_name),
    python::PythonObject::make_string(current_source_file_name),
    python::PythonObject::make_string(current_class_name),
    python::PythonObject::make_read_only_memory_view(current_metadata.data(), current_metadata.size()),
    python::PythonObject::make_read_only_memory_view(incoming_metadata.data(), incoming_metadata.size()),
    python::PythonObject::make_string(incoming_schema_name));

  if (!create_result.get_tuple_element(0U).is_true())
  {
    return nullptr;
  }
  return std::make_unique<TachyonPythonUpgrader>(create_result.get_tuple_element(1U), current_class_name);
}

void TachyonPythonUpgrader::upgrade(std::span<const std::byte> src_span, std::span<std::byte> dest_span) const
{
  const python::GilLockGuard gil_guard;

  // Ignoring the 'None' returned by the upgrader.
  std::ignore = upgrader_.call_object(
    python::PythonObject::make_read_only_memory_view(src_span.data(), src_span.size()),
    python::PythonObject::make_writable_memory_view(dest_span.data(), dest_span.size()));
}

[[nodiscard]] bool TachyonPythonUpgrader::upgrade_required() const
{
  return true;
}

[[nodiscard]] TachyonUpgraderType TachyonPythonUpgrader::upgrader_type() const
{
  return TachyonUpgraderType::python;
}

} // namespace

[[nodiscard]] std::unique_ptr<TachyonUpgrader> make_tachyon_python_upgrader(
  std::string_view current_module_name,
  std::string_view current_source_file_name,
  std::string_view current_class_name,
  std::span<const std::byte> current_metadata,
  std::span<const std::byte> incoming_metadata,
  std::string_view incoming_schema_name)
{
  const std::string current_metadata_str{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting serialized metadata from bytes to string
    reinterpret_cast<const char*>(current_metadata.data()),
    current_metadata.size()};
  auto current_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!current_proto->ParseFromString(current_metadata_str))
  {
    throw std::runtime_error("Failed to parse destination metadata");
  }
  auto current_model = TachyonModel::from_proto(std::move(current_proto));
  const std::string incoming_metadata_str{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Converting serialized metadata from bytes to string
    reinterpret_cast<const char*>(incoming_metadata.data()),
    incoming_metadata.size()};
  auto incoming_proto = std::make_unique<metadata::TachyonMetadata>();
  if (!incoming_proto->ParseFromString(incoming_metadata_str))
  {
    throw std::runtime_error("Failed to parse source metadata");
  }
  auto incoming_model = TachyonModel::from_proto(std::move(incoming_proto));
  if (auto python_upgrader = TachyonPythonUpgrader::make_upgrader(
        current_module_name,
        current_source_file_name,
        current_class_name,
        current_metadata,
        incoming_metadata,
        incoming_schema_name);
      python_upgrader != nullptr)
  {
    return python_upgrader;
  }
  return make_tachyon_memcpy_upgrader(
    current_model->get_outer_type().get_size(),
    incoming_model->get_outer_type().get_fqn(),
    current_model->get_outer_type().get_fqn());
}

} // namespace clockwork::serialization
