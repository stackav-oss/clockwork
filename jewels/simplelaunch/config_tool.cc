// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/simplelaunch/config.hh"
#include "jewels/std/expected.hh"

#include <google/protobuf/repeated_ptr_field.h>
#include <google/protobuf/text_format.h>
#include <tclap/ArgException.h>
#include <tclap/CmdLine.h>
#include <tclap/UnlabeledMultiArg.h>
#include <tclap/UnlabeledValueArg.h>
#include <tclap/ValueArg.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <memory_resource>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jewels::simplelaunch
{

namespace
{

/// Validate subcommand
constexpr auto validate_subcommand = "validate";

/// Merge subcommand
constexpr auto merge_subcommand = "merge";

} // namespace

/// Validate a configuration file
/// @param[in] args Command line arguments
/// @return 0 on success, exit code on failure
int32_t validate(std::span<char*> args)
{
  TCLAP::CmdLine cmd("validate subcommand.", ' ', "0.1", true);
  const TCLAP::UnlabeledValueArg<std::string> config_file("config-file", "Configuration file", true, "", "string", cmd);

  try
  {
    cmd.parse(static_cast<int32_t>(args.size()), args.data());
  }
  catch (const TCLAP::ArgException& exc)
  {
    log_cerr_fatal("Failed to parse command line: {}", exc.what());
    return EXIT_FAILURE;
  }

  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  filesystem::Filesystem filesys{memory_resource};
  const auto config_result = load_config(filesys, config_file.getValue());
  if (!config_result)
  {
    log_cerr_error("Failed to load config file '{}': {}", config_file.getValue(), config_result.error());
    return EXIT_FAILURE;
  }
  const auto& config = config_result.value();

  std::set<std::string> app_names;
  for (const auto& app : config.pre_launch())
  {
    if (app_names.contains(app.name()))
    {
      log_cerr_error("Duplicate app name: {}", app.name());
      return EXIT_FAILURE;
    }
    app_names.emplace(app.name());
  }

  for (const auto& app : config.app())
  {
    if (app_names.contains(app.name()))
    {
      log_cerr_error("Duplicate app name: {}", app.name());
      return EXIT_FAILURE;
    }
    app_names.emplace(app.name());
    if (app.as_root())
    {
      log_cerr_error("as_root is set to true for {} but only pre-launch apps can run as root", app.name());
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

/// Merge configuration files
/// @param[in] args Command line arguments
/// @return 0 on success, exit code on failure
int32_t merge(std::span<char*> args)
{
  TCLAP::CmdLine cmd("merge subcommand.", ' ', "0.1", true);
  const TCLAP::ValueArg<std::string> output_file("o", "output", "Output configuration file", true, "", "string", cmd);
  const TCLAP::UnlabeledMultiArg<std::string> input_files(
    "input_files", "Input configuration files", true, "string", cmd);

  try
  {
    cmd.parse(static_cast<int32_t>(args.size()), args.data());
  }
  catch (const TCLAP::ArgException& exc)
  {
    log_cerr_fatal("Failed to parse command line: {}", exc.what());
    return EXIT_FAILURE;
  }

  ::jewels::simplelaunch::v1::Config output_config;
  const memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  filesystem::Filesystem filesys{memory_resource};

  for (const auto& input_file : input_files.getValue())
  {
    const auto config_result = load_config(filesys, input_file);
    if (!config_result)
    {
      log_cerr_error("Failed to load config file '{}': {}", input_file, config_result.error());
      return EXIT_FAILURE;
    }
    const auto& config = config_result.value();
    output_config.mutable_app()->Add(config.app().begin(), config.app().end());
    output_config.mutable_pre_launch()->Add(config.pre_launch().begin(), config.pre_launch().end());
  }

  std::string output_config_str;
  if (!google::protobuf::TextFormat::PrintToString(output_config, &output_config_str))
  {
    log_cerr_fatal("Failed to serialize output config proto.");
    return EXIT_FAILURE;
  }
  auto open_result = filesys.open(output_file.getValue(), O_CREAT | O_WRONLY);
  if (!open_result)
  {
    log_cerr_fatal("Failed to open the output file: {}", open_result.error());
    return EXIT_FAILURE;
  }
  if (const auto write_result = filesys.write(open_result.value(), std::as_bytes(std::span{output_config_str}));
      !write_result)
  {
    log_cerr_fatal("Failed to write the output file: {}", write_result.error());
    return EXIT_FAILURE;
  }
  if (const auto close_result = open_result->close(); !close_result)
  {
    log_cerr_fatal("Failed to close the output file: {}", close_result.error());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

} // namespace jewels::simplelaunch

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: config_tool <sub_command> [<arg>...]" << "\n";
      return EXIT_FAILURE;
    }

    const std::span<char*> args{argv, static_cast<size_t>(argc)};
    const auto subcommand = std::string_view{args[1U]};
    if (subcommand == jewels::simplelaunch::validate_subcommand)
    {
      return jewels::simplelaunch::validate(args.last(args.size() - 1U));
    }

    if (subcommand == jewels::simplelaunch::merge_subcommand)
    {
      return jewels::simplelaunch::merge(args.last(args.size() - 1U));
    }

    jewels::log_cerr_fatal(
      "Invalid subcommnd: {}, expected {} or {}",
      subcommand,
      jewels::simplelaunch::validate_subcommand,
      jewels::simplelaunch::merge_subcommand);
    return EXIT_FAILURE;
  }
  catch (const std::exception& exc)
  {
    jewels::log_cerr_error("Caught unexcepted exception: {}", exc.what());
    return EXIT_FAILURE;
  }
}
