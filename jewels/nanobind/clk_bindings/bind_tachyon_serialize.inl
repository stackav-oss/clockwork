// IWYU pragma: private, include "jewels/nanobind/clk_bindings/bind_tachyon_serialize.hh"
#pragma once

#include "jewels/nanobind/clk_bindings/bind_tachyon_serialize.hh"

#include "clockwork/repr_iface.hh"

#include <fmt10/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string_view.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace jewels::nanobind
{

namespace detail
{
// Convenience functions for converting between clockwork::Tappy<>/nb::ndarray/std::span types.
template <typename SchemaT>
std::span<uint8_t, sizeof(clockwork::Tappy<SchemaT>)> make_tappy_span(clockwork::Tappy<SchemaT>& message)
{
  // It's cleaner to just reinterpret cast because nb::ndarray uses uint8_t, and std::as_bytes/as_writable_bytes require
  // std::byte. NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return std::span<uint8_t, sizeof(clockwork::Tappy<SchemaT>)>(reinterpret_cast<uint8_t*>(&message), sizeof(message));
}

template <typename SchemaT>
std::span<const uint8_t, sizeof(clockwork::Tappy<SchemaT>)> make_tappy_span(const clockwork::Tappy<SchemaT>& message)
{
  // It's cleaner to just reinterpret cast because nb::ndarray uses uint8_t, and std::as_bytes/as_writable_bytes require
  // std::byte. NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return std::span<const uint8_t, sizeof(clockwork::Tappy<SchemaT>)>(
    reinterpret_cast<const uint8_t*>(&message), sizeof(message));
}

template <int64_t nbytes>
std::span<uint8_t, nbytes> make_ndarray_span(::nanobind::ndarray<uint8_t, ::nanobind::shape<nbytes>>& message)
{
  return std::span<uint8_t, nbytes>{message.data(), message.size()};
}

template <int64_t nbytes>
std::span<const uint8_t, nbytes>
make_ndarray_span(const ::nanobind::ndarray<const uint8_t, ::nanobind::shape<nbytes>>& message)
{
  return std::span<const uint8_t, nbytes>{message.data(), message.size()};
}

template <typename SchemaT, typename ndarray_type>
void tappy_to_ndarray(const clockwork::Tappy<SchemaT>& value, ndarray_type& memoryview_handle)
{
  if (sizeof(clockwork::Tappy<SchemaT>) != memoryview_handle.nbytes())
  {
    throw std::runtime_error(
      fmt::format(
        "serialize_tachyon: sizeof(clockwork::Tappy<T>) {} != memoryview size {}",
        sizeof(clockwork::Tappy<SchemaT>),
        memoryview_handle.nbytes()));
  }

  const std::span<const uint8_t, sizeof(clockwork::Tappy<SchemaT>)> message_span = detail::make_tappy_span(value);
  const std::span<uint8_t, sizeof(clockwork::Tappy<SchemaT>)> ndarray_span =
    detail::make_ndarray_span(memoryview_handle);
  std::ranges::copy(message_span, std::begin(ndarray_span));
}

template <typename SchemaT, typename const_ndarray_type>
void ndarray_to_tappy(clockwork::Tappy<SchemaT>& value, const const_ndarray_type& memoryview_handle)
{
  if (sizeof(clockwork::Tappy<SchemaT>) != memoryview_handle.nbytes())
  {
    throw std::runtime_error(
      fmt::format(
        "serialize_tachyon: sizeof(clockwork::Tappy<T>) {} != memoryview size {}",
        sizeof(clockwork::Tappy<SchemaT>),
        memoryview_handle.nbytes()));
  }

  const std::span<const uint8_t, sizeof(clockwork::Tappy<SchemaT>)> ndarray_span =
    detail::make_ndarray_span(memoryview_handle);
  const std::span<uint8_t, sizeof(clockwork::Tappy<SchemaT>)> value_span = detail::make_tappy_span(value);
  std::ranges::copy(ndarray_span, std::begin(value_span));
}

} // namespace detail

template <typename SchemaT>
void bind_tachyon_serialize(::nanobind::class_<clockwork::Tappy<SchemaT>>& cls)
{
  namespace nb = ::nanobind;
  using ndarray_type = nb::ndarray<uint8_t, nb::shape<sizeof(clockwork::Tappy<SchemaT>)>>;
  using const_ndarray_type = nb::ndarray<const uint8_t, nb::shape<sizeof(clockwork::Tappy<SchemaT>)>>;

  cls.def(
    "serialize_tachyon",
    [](const clockwork::Tappy<SchemaT>& value, ndarray_type& memoryview_handle) -> void
    { detail::tappy_to_ndarray<SchemaT, ndarray_type>(value, memoryview_handle); },
    nb::arg("buffer"));

  cls.def_static(
    "deserialize_tachyon",
    [](const const_ndarray_type& memoryview_handle) -> clockwork::Tappy<SchemaT>*
    {
      auto result_ptr = std::make_unique<clockwork::Tappy<SchemaT>>();
      detail::ndarray_to_tappy<SchemaT, const_ndarray_type>(*result_ptr, memoryview_handle);
      return result_ptr.release();
    },
    nb::arg("buffer"));

  cls.def(
    "__getstate__",
    [](const clockwork::Tappy<SchemaT>& value) -> nb::bytes
    {
      constexpr int64_t nbytes = sizeof(clockwork::Tappy<SchemaT>);
      // Put a temporary buffer on the heap, because clockwork::Tappy types can be too large for the stack.
      std::vector<uint8_t> vec(nbytes);
      ndarray_type memoryview_handle(vec.data(), {nbytes}, nb::handle());
      detail::tappy_to_ndarray<SchemaT, ndarray_type>(value, memoryview_handle);
      // bytes() copies under the hood so it's valid when the std::vector is freed.
      // It actually just calls https://docs.python.org/3/c-api/bytes.html#c.PyBytes_FromStringAndSize.
      return nb::bytes(vec.data(), vec.size());
    });

  cls.def(
    "__setstate__",
    [](clockwork::Tappy<SchemaT>& value, const nb::bytes& bytes) -> void
    {
      const_ndarray_type memoryview_handle(bytes.data(), {bytes.size()}, nb::handle());
      static_assert(std::is_trivially_copyable_v<clockwork::Tappy<SchemaT>>);
      detail::ndarray_to_tappy<SchemaT, const_ndarray_type>(value, memoryview_handle);
    });
}

template <typename SchemaT>
void bind_tachyon_constraint_and_metadata(
  ::nanobind::class_<clockwork::Tappy<SchemaT>>& cls,
  std::string_view tachyon_metadata_name,
  const int64_t tachyon_constraint_size,
  const int64_t tachyon_constraint_alignment,
  std::string_view tachyon_module_name,
  std::string_view tachyon_source_file_name,
  std::string_view tachyon_class_name)
{
  namespace nb = ::nanobind;

  // Sanity check size.
  if (tachyon_constraint_size != sizeof(clockwork::Tappy<SchemaT>))
  {
    throw std::runtime_error(
      fmt::format(
        "Error: tachyon_constraint.size {} != sizeof(clockwork::Tappy<SchemaT>) {}",
        tachyon_constraint_size,
        sizeof(clockwork::Tappy<SchemaT>)));
  }

  // Sanity check alignment.
  if (tachyon_constraint_alignment != alignof(clockwork::Tappy<SchemaT>))
  {
    throw std::runtime_error(
      fmt::format(
        "Error: tachyon_constraint.alignment {} != alignof(clockwork::Tappy<SchemaT>) {}",
        tachyon_constraint_alignment,
        alignof(clockwork::Tappy<SchemaT>)));
  }

  // store the metadata name as a class attribute
  setattr(
    cls,
    "__tachyon_metadata_name",
    nb::detail::type_caster<std::string_view>::from_cpp(tachyon_metadata_name, nb::rv_policy::automatic, nullptr));

  // Define get_tachyon_metadata_name(), which simply retrieves the metadata name
  cls.def_static(
    "get_tachyon_metadata_name",
    []() -> nb::object
    {
      nb::handle class_handle = nb::type<clockwork::Tappy<SchemaT>>();
      return nb::getattr(class_handle, "__tachyon_metadata_name");
    },
    nb::sig("def get_tachyon_metadata_name() -> str"));

  // deserialize the metadata to clockwork::TachyonMetadata and store it as a class attribute
  nb::bytes metadata_bytes(
    ::clockwork::LoggingTraits<clockwork::Tappy<SchemaT>>::schema_definition.data(),
    ::clockwork::LoggingTraits<clockwork::Tappy<SchemaT>>::schema_definition.size());
  setattr(
    cls,
    "__tachyon_metadata",
    nb::module_::import_("clockwork.serialization.metadata.tachyon")
      .attr("get_metadata_from_protobuf")(metadata_bytes));

  // Define get_tachyon_metadata(), which simply retrieves the metadata
  cls.def_static(
    "get_tachyon_metadata",
    []() -> nb::object
    {
      nb::handle class_handle = nb::type<clockwork::Tappy<SchemaT>>();
      return nb::getattr(class_handle, "__tachyon_metadata");
    },
    nb::sig(
      "def get_tachyon_metadata() -> "
      "clockwork.serialization.metadata.tachyon_model.TachyonMetadata"));

  // Define get_tachyon_constraint(), which forms the constraint on demand
  cls.def_static(
    "get_tachyon_constraint",
    []() -> nb::object
    {
      return nb::module_::import_("clockwork.dsl.serialization.tachyon_reg")
        .attr("FieldConstraint")(sizeof(clockwork::Tappy<SchemaT>), alignof(clockwork::Tappy<SchemaT>));
    },
    nb::sig(
      "def get_tachyon_constraint() -> "
      "clockwork.dsl.serialization.tachyon_reg.FieldConstraint"));

  // store the module name as a class attribute
  setattr(
    cls,
    "__tachyon_module_name",
    nb::detail::type_caster<std::string_view>::from_cpp(tachyon_module_name, nb::rv_policy::automatic, nullptr));

  // Define get_tachyon_module_name(), which simply retrieves the module name
  cls.def_static(
    "get_tachyon_module_name",
    []() -> nb::object
    {
      nb::handle class_handle = nb::type<clockwork::Tappy<SchemaT>>();
      return nb::getattr(class_handle, "__tachyon_module_name");
    },
    nb::sig("def get_tachyon_module_name() -> str"));

  // store the source file name as a class attribute
  setattr(
    cls,
    "__tachyon_source_file_name",
    nb::detail::type_caster<std::string_view>::from_cpp(tachyon_source_file_name, nb::rv_policy::automatic, nullptr));

  // Define get_tachyon_source_file_name(), which simply retrieves the source file name
  cls.def_static(
    "get_tachyon_source_file_name",
    []() -> nb::object
    {
      nb::handle class_handle = nb::type<clockwork::Tappy<SchemaT>>();
      return nb::getattr(class_handle, "__tachyon_source_file_name");
    },
    nb::sig("def get_tachyon_source_file_name() -> str"));

  // store the class name as a class attribute
  setattr(
    cls,
    "__tachyon_class_name",
    nb::detail::type_caster<std::string_view>::from_cpp(tachyon_class_name, nb::rv_policy::automatic, nullptr));

  // Define get_tachyon_class_name(), which simply retrieves the class name
  cls.def_static(
    "get_tachyon_class_name",
    []() -> nb::object
    {
      nb::handle class_handle = nb::type<clockwork::Tappy<SchemaT>>();
      return nb::getattr(class_handle, "__tachyon_class_name");
    },
    nb::sig("def get_tachyon_class_name() -> str"));
}

} // namespace jewels::nanobind
