// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/offboard/merge_logs.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"

#include <tclap/CmdLine.h>
#include <tclap/MultiArg.h>
#include <tclap/ValueArg.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

int main(int32_t argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Make merge union", ' ', "1.0", true);
    const TCLAP::MultiArg<std::string> input_uris_arg("i", "input", "Input log URI ", true, "uri", cmd);
    const TCLAP::ValueArg<std::string> output_uri_arg("o", "output", "Output log URI", true, "", "uri", cmd);

    cmd.parse(argc, argv);

    const auto& input_uris = input_uris_arg.getValue();
    const auto& output_uri = output_uri_arg.getValue();

    const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};

    std::vector<std::string_view> input_uri_views{input_uris.begin(), input_uris.end()};
    if (const auto write_result =
          clockwork_logging::offboard::write_merge_union(memory_resource, input_uri_views, output_uri);
        !write_result)
    {
      jewels::log_cerr_error("Failed to make the merge union: {}", write_result.error());
      return 1;
    }
    std::cout << "Success:\n";
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
