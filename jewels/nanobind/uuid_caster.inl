// IWYU pragma: private, include "jewels/nanobind/uuid_caster.hh"
#pragma once
#include "jewels/std/expected.hh"
#include "jewels/uuid/uuid.hh"

#include <nanobind/eval.h>
#include <nanobind/nanobind.h>

#include <cstdint>
#include <string_view>

NAMESPACE_BEGIN(NB_NAMESPACE)
NAMESPACE_BEGIN(detail)

template <typename TagType>
struct type_caster<jewels::Uuid<TagType>>
{
  using UuidT = jewels::Uuid<TagType>;
  NB_TYPE_CASTER(UuidT, const_name("uuid.UUID"))

  bool from_python(handle src, uint8_t /* flags */, cleanup_list* /* cleanup */) noexcept
  {
    const str uuid_str(src);
    const jewels::expected<UuidT, std::string_view> maybe_uuid = UuidT::from_string(std::string_view(uuid_str.c_str()));
    if (!maybe_uuid.has_value())
    {
      return false;
    }

    value = maybe_uuid.value();
    return true;
  }

  static handle from_cpp(const UuidT& val, rv_policy /* policy */, cleanup_list* /* cleanup */) noexcept
  {
    // Ignore policy because UUIDs are treated as immutable.
    auto global_scope = module_::import_("__main__").attr("__dict__");
    const dict local_scope;
    local_scope["uuid_bytes"] = bytes(val.uuid.data(), val.uuid.size());
    exec(
      R"(
        import uuid
        uuid_value = uuid.UUID(bytes=uuid_bytes)
        )",
      global_scope,
      local_scope);

    return object{local_scope["uuid_value"]}.release();
  }
};

NAMESPACE_END(detail)
NAMESPACE_END(NB_NAMESPACE)
