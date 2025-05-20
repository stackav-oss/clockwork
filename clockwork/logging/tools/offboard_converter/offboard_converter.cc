// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/nolint_helper.hh"
#include "clockwork/logging/offboard/chunk_reader_writer_factory.hh"
#include "clockwork/logging/tools/offboard_converter/convert_to_offboard.hh"
#include "jewels/memory/memory_resource.hh"

#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>
#include <wise_enum.h>

#include <exception>
#include <iostream>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

int main(int argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Convert a log to offboard format", ' ', "1.0", true);
    const TCLAP::UnlabeledValueArg<std::string> source_path_arg(
      "source_path", "Source log path", true, "", "log path", cmd);
    const TCLAP::UnlabeledValueArg<std::string> offboard_path_arg(
      "offboard_path", "Offboard log path", true, "", "log path", cmd);
    const TCLAP::ValueArg<std::string> writer_config_pbtxt_arg(
      "c", "writer_config_pbtxt", "Path to log writer config text protobuf", false, "", "log path", cmd);

    try
    {
      cmd.parse(argc, argv);
    }
    catch (const TCLAP::ArgException& exc)
    {
      std::cerr << "Failed to parse command line: " << exc.what() << '\n';
      return 1;
    }

    std::string writer_config_pbtxt;
    if (!writer_config_pbtxt_arg.getValue().empty())
    {
      const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
      clockwork_logging::offboard::ChunkReaderWriterFactory chunk_reader_writer_factory(memory_resource);
      const auto read_result = chunk_reader_writer_factory.read_log_file(writer_config_pbtxt_arg.getValue());
      if (!read_result)
      {
        std::cerr << "Failed to read the writer config file from '" << writer_config_pbtxt_arg.getValue()
                  << "': " << wise_enum::to_string(read_result.error()) << "\n";
        return 1;
      }
      writer_config_pbtxt =
        std::string{clockwork_logging::nolint_helper::byte_span_to_string_view(read_result.value())};
    }

    clockwork_logging::convert_to_offboard(
      source_path_arg.getValue(), offboard_path_arg.getValue(), writer_config_pbtxt);
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
