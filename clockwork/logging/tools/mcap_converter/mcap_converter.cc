// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/tools/mcap_converter/convert_to_mcap.hh"

#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Convert a log to MCAP format", ' ', "1.0", true);
    const TCLAP::UnlabeledValueArg<std::string> source_path_arg(
      "source_path", "Source log path", true, "", "log path", cmd);
    const TCLAP::UnlabeledValueArg<std::string> mcap_path_arg("mcap_path", "MCAP log path", true, "", "log path", cmd);

    try
    {
      cmd.parse(argc, argv);
    }
    catch (const TCLAP::ArgException& exc)
    {
      std::cerr << "Failed to parse command line: " << exc.what() << '\n';
      return 1;
    }

    clockwork_logging::convert_to_mcap(source_path_arg.getValue(), mcap_path_arg.getValue());
    return 0;
  }
  catch (const std::exception& exc)
  {
    std::cerr << "Caught unexpected exception: " << exc.what() << '\n';
    return 1;
  }
}
