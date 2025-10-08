// IWYU pragma: private, include "clockwork/memory/start_lifetime_as.hh"
#pragma once

#include "clockwork/memory/start_lifetime_as.hh"

#include "jewels/memory/pointers.hh"
#include "jewels/meta/call.hh"
#include "jewels/meta/concepts.hh"
#include "jewels/meta/type_traits.hh"

#include <cstring>
#include <new>
#include <span>
#include <type_traits>

namespace clockwork
{

template <jewels::meta::ImplicitLifetimeType Type, jewels::meta::Byte Byte>
  requires(std::is_const_v<Type> || !std::is_const_v<Byte>)
jewels::memory::ObjectPtr<Type> start_lifetime_as(std::span<Byte, sizeof(Type)> bytes) noexcept
{
  using VoidType = jewels::meta::Call<jewels::meta::ConditionalT<std::is_const_v<Byte>>, const void, void>;
  VoidType* ptr = bytes.data();
  // Using memmove here as that is a valid way to start the lifetime
  // of an object.  Use of memmove permits src and dest to be the
  // same.  However, in this case, the dest must be mutable.  Using
  // const_cast to allow this given the result of this should not
  // change the values.  Otherwise this wouldn't be able to be used
  // for const inputs.
  return jewels::memory::ObjectPtr<Type>{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) Not modifying underlying ptr.
    std::launder(static_cast<Type*>(std::memmove(const_cast<void*>(ptr), ptr, sizeof(Type))))};
}

} // namespace clockwork
