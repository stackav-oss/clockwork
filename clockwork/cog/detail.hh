// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <tuple>

namespace clockwork::detail
{

///
/// Call the validate on each member of the object tuple, logging an error if it fails
/// @tparam PolicyContainer a template to be used as PolicyContainer<Policy>
/// @param objects a tuple of PolicyContainer<> objects
///
template <template <typename> typename PolicyContainer, typename... Policies, typename Validator>
bool validate_helper(const std::tuple<PolicyContainer<Policies>...>& objects, Validator&& validate);

} // namespace clockwork::detail

#include "clockwork/cog/detail.inl"
