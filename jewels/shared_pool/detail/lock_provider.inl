// IWYU pragma: private, include "jewels/shared_pool/detail/lock_provider.hh"
#pragma once

#include "jewels/shared_pool/detail/lock_provider.hh"

#include <mutex>

namespace jewels::detail
{

[[nodiscard]] char NullLockProvider::lock()
{
  return 'X';
}

[[nodiscard]] std::lock_guard<std::mutex> MutexLockProvider::lock()
{
  return std::lock_guard{mutex_};
}

} // namespace jewels::detail
