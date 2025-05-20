// IWYU pragma: private, include "jewels/compiler/intrinsics.hh"
#pragma once

#include "jewels/compiler/intrinsics.hh"

#include <utility>

namespace jewels
{

template <class BoolLike>
inline bool unlikely(BoolLike&& bool_like) noexcept
{
  return __builtin_expect(static_cast<bool>(std::forward<BoolLike>(bool_like)), false);
}

} // namespace jewels
