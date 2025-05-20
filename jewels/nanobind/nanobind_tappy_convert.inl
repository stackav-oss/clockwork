// IWYU pragma: private, include "jewels/nanobind/nanobind_tappy_convert.hh"

#include <span>
#pragma once
#include "clockwork/repr_iface.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/nanobind/nanobind_tappy_convert.hh"
#include "jewels/std/span.hh"

#include <Python.h>
#include <fmt10/format.h>
#include <nanobind/eval.h>
#include <nanobind/nanobind.h>
#include <wise_enum.h> // IWYU pragma: keep

#include <algorithm>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace jewels
{
namespace detail
{
// Convenience functions for converting between clockwork::Tappy<>/nb::bytes/std::span types.
template <typename SchemaT>
std::span<std::byte, sizeof(clockwork::Tappy<SchemaT>)> make_tappy_span(clockwork::Tappy<SchemaT>& message)
{
  return std::as_writable_bytes(jewels::as_single_item_span(message));
}

template <typename SchemaT>
std::span<const std::byte, sizeof(clockwork::Tappy<SchemaT>)> make_tappy_span(const clockwork::Tappy<SchemaT>& message)
{
  return std::as_bytes(jewels::as_single_item_span(message));
}

std::span<const std::byte> make_bytes_span(const nanobind::bytes& message)
{
  return std::as_bytes(std::span{message.c_str(), message.size()});
}

template <typename SchemaT>
nanobind::bytes make_bytes_from_tappy(const clockwork::Tappy<SchemaT>& message)
{
  const std::span<const std::byte, sizeof(clockwork::Tappy<SchemaT>)> message_span = make_tappy_span<SchemaT>(message);
  // This reinterpret cast is necessary because nb::bytes uses char and std::as_bytes/as_writable_bytes uses std::byte.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return nanobind::bytes(reinterpret_cast<const char*>(message_span.data()), message_span.size());
}

template <typename SchemaT>
void deserialize_into_tappy_from_py(clockwork::Tappy<SchemaT>& tappy_value, const nanobind::handle& python_object)
{
  namespace nb = nanobind;

  try
  {
    // convert python_object to serialized bytes
    auto global_scope = nb::module_::import_("__main__").attr("__dict__");
    const nb::dict local_scope;
    local_scope["python_object"] = python_object;
    exec(
      R"(
      bytes_from_py = bytearray(python_object.get_tachyon_constraint().size)
      python_object.serialize_tachyon(memoryview(bytes_from_py))
      payload = bytes(bytes_from_py)
      )",
      global_scope,
      local_scope);
    const nb::bytes payload{local_scope["payload"]};

    if (!payload.is_valid())
    {
      throw std::runtime_error("Error deserializing tappy from python object.");
    }

    // convert bytes to serialized message
    if (payload.size() != sizeof(tappy_value))
    {
      throw std::runtime_error(fmt::format(
        "Error deserializing tappy from python: python serialized bytes is wrong size, "
        "sizeof(clockwork::Tappy<SchemaT>): "
        "{}, "
        "python buffer size {}",
        sizeof(tappy_value),
        payload.size()));
    }

    // copy the bytes
    std::ranges::copy(detail::make_bytes_span(payload), std::begin(detail::make_tappy_span(tappy_value)));
  }
  catch (const std::exception& exn)
  {
    throw std::runtime_error(fmt::format("Error deserializing tappy from python: {}", exn.what()));
  }
}
} // namespace detail

template <typename SchemaT>
clockwork::Tappy<SchemaT> deserialize_tappy_from_py(const nanobind::handle& python_object)
{
  clockwork::Tappy<SchemaT> value;
  detail::deserialize_into_tappy_from_py(value, python_object);
  return value;
}

template <typename SchemaT>
memory::pmr_unique_ptr<clockwork::Tappy<SchemaT>>
deserialize_tappy_ptr_from_py(const nanobind::handle& python_object, memory::MemoryResource memory_resource)
{
  memory::pmr_unique_ptr<clockwork::Tappy<SchemaT>> value =
    memory::make_pmr_unique<clockwork::Tappy<SchemaT>>(memory_resource);
  detail::deserialize_into_tappy_from_py(*value, python_object);
  return value;
}

template <typename SchemaT>
nanobind::handle serialize_tappy_to_py(
  const clockwork::Tappy<SchemaT>& message, const std::string& clkpy_module_name, const std::string& clkpy_class_name)
{
  namespace nb = nanobind;

  // import the python module defining the clockwork type
  nb::module_::import_(clkpy_module_name.c_str());

  // copy clockwork::Tappy message to python bytes
  nb::bytes payload = detail::make_bytes_from_tappy<SchemaT>(message);

  // convert bytes to python message
  auto global_scope = nb::module_::import_("__main__").attr("__dict__");
  const nb::dict local_scope;
  local_scope["payload"] = payload;
  local_scope["clkpy_class_name"] = nb::str(clkpy_class_name.data(), clkpy_class_name.size());
  local_scope["clkpy_module_name"] = nb::str(clkpy_module_name.data(), clkpy_module_name.size());
  exec(
    R"(
      import importlib
      clkpy_module = importlib.import_module(clkpy_module_name)
      message_cls = getattr(clkpy_module, clkpy_class_name)
      msg = message_cls.deserialize_tachyon(memoryview(payload))
      )",
    global_scope,
    local_scope);

  return nb::object{local_scope["msg"]}.release();
}

template <typename EnumT>
bool deserialize_enum_from_py(const nanobind::handle& python_object, EnumT& value)
{
  static_assert(wise_enum::is_wise_enum_v<EnumT>, "Type must be a wise_enum!");

  namespace nb = nanobind;

  try
  {
    // Make sure the Python object is an Enum.
    const nb::handle enum_type = nb::module_::import_("enum").attr("Enum");
    if (!PyObject_IsInstance(python_object.ptr(), enum_type.ptr()))
    {
      // It's not an enum.
      return false;
    }

    // Get the underlying integer value from python and cast it to the C++ enum.
    using UnderlyingType = std::underlying_type_t<EnumT>;
    const auto int_value = nb::cast<UnderlyingType>(python_object.attr("value"));
    value = static_cast<EnumT>(int_value);
    return true;
  }
  catch (const std::exception& exn)
  {
    throw std::runtime_error(fmt::format("Error deserializing tappy from python: {}", exn.what()));
  }
}

template <typename EnumT>
nanobind::handle
serialize_enum_to_py(const EnumT message, const std::string& clkpy_module_name, const std::string& clkpy_class_name)
{
  static_assert(wise_enum::is_wise_enum_v<EnumT>, "Type must be a wise_enum!");

  namespace nb = nanobind;

  // convert enum to underlying integer
  using UnderlyingType = std::underlying_type_t<EnumT>;
  const auto enum_value = static_cast<UnderlyingType>(message);

  // import the python module defining the clockwork type
  nb::module_::import_(clkpy_module_name.c_str());

  // convert int value to python value
  auto global_scope = nb::module_::import_("__main__").attr("__dict__");
  const nb::dict local_scope;
  local_scope["enum_value"] = enum_value;
  local_scope["clkpy_class_name"] = nb::str(clkpy_class_name.data(), clkpy_class_name.size());
  local_scope["clkpy_module_name"] = nb::str(clkpy_module_name.data(), clkpy_module_name.size());
  exec(
    R"(
      import importlib
      clkpy_module = importlib.import_module(clkpy_module_name)
      message_cls = getattr(clkpy_module, clkpy_class_name)
      msg = message_cls(enum_value)
      )",
    global_scope,
    local_scope);

  return nb::object{local_scope["msg"]}.release();
}

} // namespace jewels
