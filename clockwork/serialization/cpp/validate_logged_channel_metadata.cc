// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/serialization/cpp/tachyon_model.hh"
#include "clockwork/serialization/metadata/tachyon_model.pb.h"
#include "jewels/log_cerr/log_cerr.hh"

#include <tclap/CmdLine.h>
#include <tclap/UnlabeledValueArg.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>

int main(int argc, char* argv[])
{
  try
  {
    TCLAP::CmdLine cmd("Validate logged metadata", ' ', "1.0", true);
    const TCLAP::UnlabeledValueArg<std::string> prev_metadata_file_arg(
      "prev-metadata-file", "File", true, "", "path", cmd);
    const TCLAP::UnlabeledValueArg<std::string> curr_metadata_file_arg(
      "curr-metadata-file", "File", true, "", "path", cmd);

    cmd.parse(argc, argv);

    const auto& prev_metadata_file = prev_metadata_file_arg.getValue();
    const auto& curr_metadata_file = curr_metadata_file_arg.getValue();

    clockwork::serialization::metadata::LoggedChannelMetadata prev_metadata;
    std::ifstream prev_input(prev_metadata_file, std::ios::in | std::ios::binary);
    if (!prev_input)
    {
      jewels::log_cerr_error("Failed to open prev metadata file");
      return EXIT_FAILURE;
    }
    if (!prev_metadata.ParseFromIstream(&prev_input))
    {
      jewels::log_cerr_error("Failed to parse prev metadata");
      return EXIT_FAILURE;
    }

    clockwork::serialization::metadata::LoggedChannelMetadata curr_metadata;
    std::ifstream curr_input(curr_metadata_file, std::ios::in | std::ios::binary);
    if (!curr_input)
    {
      jewels::log_cerr_error("Failed to open curr metadata file");
      return EXIT_FAILURE;
    }
    if (!curr_metadata.ParseFromIstream(&curr_input))
    {
      jewels::log_cerr_error("Failed to parse curr metadata");
      return EXIT_FAILURE;
    }

    if (!clockwork::serialization::validate_logged_channel_metadata(prev_metadata, curr_metadata))
    {
      jewels::log_cerr_error("Metadata validation failed");
      return EXIT_FAILURE;
    }
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("Caught unexpected exception: {}", exc.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
