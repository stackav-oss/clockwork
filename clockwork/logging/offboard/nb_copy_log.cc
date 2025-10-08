// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/copy_log.hh"
#include "clockwork/logging/readers/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <fmt10/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/list.h>        // IWYU pragma: keep
#include <nanobind/stl/optional.h>    // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep

#include <functional>
#include <list>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

NB_MODULE(nb_copy_log, mod)
{
  nanobind::module_::import_("clockwork.logging.readers.nb_types");

  mod.doc() = "Nanobind copy log wrapper";

  mod.def(
    "copy_log",
    [](
      std::string_view source_uri,
      std::string_view dest_uri,
      std::optional<std::list<std::string>> maybe_desired_channels,
      std::optional<std::list<std::string>> maybe_excluded_channels,
      std::optional<clockwork_logging::RelativeInterval> maybe_log_interval,
      std::string_view writer_config_str,
      bool no_deep_copy)
    {
      const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
      std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_desired_set;
      if (maybe_desired_channels)
      {
        maybe_desired_set.emplace(memory_resource);
        for (const auto& channel : maybe_desired_channels.value())
        {
          maybe_desired_set.value().emplace(channel);
        }
      }
      std::optional<std::pmr::unordered_set<std::pmr::string>> maybe_excluded_set;
      if (maybe_excluded_channels)
      {
        maybe_excluded_set.emplace(memory_resource);
        for (const auto& channel : maybe_excluded_channels.value())
        {
          maybe_excluded_set.value().emplace(channel);
        }
      }
      if (const auto copy_result = clockwork_logging::offboard::copy_log(/*memory_resource=*/memory_resource,
                                                                         /*source_uri=*/source_uri,
                                                                         /*dest_uri=*/dest_uri,
                                                                         /*maybe_desired_channels=*/maybe_desired_set,
                                                                         /*maybe_excluded_channels=*/maybe_excluded_set,
                                                                         /*maybe_log_interval=*/maybe_log_interval,
                                                                         /*writer_config_str=*/writer_config_str,
                                                                         /*no_deep_copy=*/no_deep_copy);
          !copy_result)
      {
        throw std::runtime_error(fmt::format("Failed to copy log: {}", copy_result.error()));
      }
    },
    nanobind::arg("source_uri"),
    nanobind::arg("dest_uri"),
    nanobind::arg("maybe_desired_channels") = nanobind::none(),
    nanobind::arg("maybe_excluded_channels") = nanobind::none(),
    nanobind::arg("maybe_log_interval") = nanobind::none(),
    nanobind::arg("writer_config_str") = std::string_view{},
    nanobind::arg("no_deep_copy") = false,
    "Copy a log.");
}
