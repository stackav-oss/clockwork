// IWYU pragma: private, include "clockwork/cog/detail.hh"
#pragma once

#include "jewels/log_cerr/log_cerr.hh"

#include <tuple>

namespace clockwork::detail
{

template <template <typename> typename PolicyContainer, typename... Policies, typename Validator>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
bool validate_helper(const std::tuple<PolicyContainer<Policies>...>& objects, Validator&& validate)
{
  auto do_validate = [&validate]<typename Policy>(const PolicyContainer<Policy>& object)
  {
    if (!validate(object))
    {
      jewels::log_cerr_error("cog validation failure: {}", Policy::name);
      return false;
    }
    return true;
  };
  return std::apply([&](auto&... object) { return (do_validate(object) && ...); }, objects);
}

} // namespace clockwork::detail
