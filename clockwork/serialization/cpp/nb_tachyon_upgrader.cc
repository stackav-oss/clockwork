// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/cpp/tachyon_python_upgrader.hh"
#include "clockwork/serialization/cpp/tachyon_upgrader.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/log_cerr/log_cerr.hh"

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>         // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <wise_enum.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <utility>

namespace
{

/// Wrapper class to hold a pointer to a tachyon C++ upgrader
struct TachyonCppUpgraderWrapper
{
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
};

/// Wrapper class to hold a pointer to a tachyon python upgrader
struct TachyonPythonUpgraderWrapper
{
  std::unique_ptr<clockwork::serialization::TachyonUpgrader> upgrader;
};

/// Validate that the logged channel schemas can be upgraded from a previous version to the current version
/// @param[in] prev_metadata_str Previous serialized logged channel metadata
/// @param[in] curr_metadata_str Current serialized logged channel metadata
/// @returns True if all metadata can up upgraded
[[nodiscard]] bool
validate_logged_channel_metadata(const std::string_view prev_metadata_str, const std::string_view curr_metadata_str)
{
  clockwork::serialization::metadata::LoggedChannelMetadata prev_metadata;
  if (!prev_metadata.ParseFromString(prev_metadata_str))
  {
    jewels::log_cerr_error("Failed to deserialize previous metadata");
    return false;
  }

  clockwork::serialization::metadata::LoggedChannelMetadata curr_metadata;
  if (!curr_metadata.ParseFromString(curr_metadata_str))
  {
    jewels::log_cerr_error("Failed to deserialize current metadata");
    return false;
  }

  return clockwork::serialization::validate_logged_channel_metadata(prev_metadata, curr_metadata);
}

} // namespace

NB_MODULE(nb_tachyon_upgrader, mod)
{
  nanobind::set_leak_warnings(false);
  mod.doc() = "Tachyon upgrader python wrappers";

  // Tachyon C++ upgrader bindings
  nanobind::class_<TachyonCppUpgraderWrapper>(mod, "TachyonCppUpgrader")
    .def(
      "__init__",
      [](
        TachyonCppUpgraderWrapper* ptr,
        std::string_view current_class_name,
        const nanobind::bytes& current_metadata,
        const nanobind::bytes& incoming_metadata)
      {
        auto upgrader = clockwork::serialization::make_tachyon_cpp_upgrader(
          current_class_name,
          std::span{static_cast<const std::byte*>(current_metadata.data()), current_metadata.size()},
          std::span{static_cast<const std::byte*>(incoming_metadata.data()), incoming_metadata.size()});
        new (ptr) TachyonCppUpgraderWrapper{std::move(upgrader)};
      },
      "Constructor")
    .def(
      "upgrade",
      [](
        const TachyonCppUpgraderWrapper& obj,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& src_data,
        const nanobind::ndarray<nanobind::numpy, uint8_t, nanobind::shape<-1>>& dest_data)
      {
        obj.upgrader->upgrade(
          std::as_bytes(std::span{src_data.data(), src_data.nbytes()}),
          std::as_writable_bytes(std::span{dest_data.data(), dest_data.nbytes()}));
      },
      nanobind::sig("def upgrade(self, src_data: memory_view, dest_data: memory_view) -> None"),
      "Upgrade message to current schema")
    .def_prop_ro(
      "upgrade_required",
      [](const TachyonCppUpgraderWrapper& obj) { return obj.upgrader->upgrade_required(); },
      "Upgrade required.")
    .def_prop_ro(
      "upgrader_type",
      [](const TachyonCppUpgraderWrapper& obj) { return wise_enum::to_string(obj.upgrader->upgrader_type()); },
      "Upgrader type.");

  // Tachyon python upgrader bindings
  nanobind::class_<TachyonPythonUpgraderWrapper>(mod, "TachyonPythonUpgrader")
    .def(
      "__init__",
      [](
        TachyonPythonUpgraderWrapper* ptr,
        std::string_view current_module_name,
        std::string_view current_source_file_name,
        std::string_view current_class_name,
        const nanobind::bytes& current_metadata,
        const nanobind::bytes& incoming_metadata,
        std::string_view incoming_schema_name)
      {
        auto upgrader = clockwork::serialization::make_tachyon_python_upgrader(
          current_module_name,
          current_source_file_name,
          current_class_name,
          std::span{static_cast<const std::byte*>(current_metadata.data()), current_metadata.size()},
          std::span{static_cast<const std::byte*>(incoming_metadata.data()), incoming_metadata.size()},
          incoming_schema_name);
        new (ptr) TachyonPythonUpgraderWrapper{std::move(upgrader)};
      },
      "Constructor")
    .def(
      "upgrade",
      [](
        const TachyonPythonUpgraderWrapper& obj,
        const nanobind::ndarray<nanobind::numpy, const uint8_t, nanobind::shape<-1>>& src_data,
        const nanobind::ndarray<nanobind::numpy, uint8_t, nanobind::shape<-1>>& dest_data)
      {
        obj.upgrader->upgrade(
          std::as_bytes(std::span{src_data.data(), src_data.nbytes()}),
          std::as_writable_bytes(std::span{dest_data.data(), dest_data.nbytes()}));
      },
      nanobind::sig("def upgrade(self, src_data: memory_view, dest_data: memory_view) -> None"),
      "Upgrade message to current schema")
    .def_prop_ro(
      "upgrade_required",
      [](const TachyonPythonUpgraderWrapper& obj) { return obj.upgrader->upgrade_required(); },
      "Upgrade required.")
    .def_prop_ro(
      "upgrader_type",
      [](const TachyonPythonUpgraderWrapper& obj) { return wise_enum::to_string(obj.upgrader->upgrader_type()); },
      "Upgrader type.");

  mod.def(
    "validate_logged_channel_metadata",
    [](const nanobind::bytes& prev_metadata, const nanobind::bytes& curr_metadata)
    {
      return validate_logged_channel_metadata(
        std::string_view(static_cast<const char*>(prev_metadata.data()), prev_metadata.size()),
        std::string_view(static_cast<const char*>(curr_metadata.data()), curr_metadata.size()));
    },
    "Check that the previous logged channel metadata can be upgraded to the current logged channel metadata.");
}
