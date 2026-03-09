// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/tools/channel_spy/channel_spy.hh"
#include "clockwork/tools/channel_spy/channel_spy_config_clk_cc.hh"
#include "clockwork/tools/channel_spy/types.hh"

#include <Python.h>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>         // IWYU pragma: keep
#include <nanobind/stl/function.h>    // IWYU pragma: keep
#include <nanobind/stl/string.h>      // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep
#include <nanobind/stl/vector.h>      // IWYU pragma: keep

#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// NOLINTNEXTLINE(readability-function-cognitive-complexity) Better to leave this than break it up
NB_MODULE(nb_channel_spy, mod)
{
  namespace nb = ::nanobind;
  using namespace nb::literals; // NOLINT(google-build-using-namespace) Needed to use nanobind literals
  nb::set_leak_warnings(false);

  mod.doc() = "Test support helper python wrapper";

  // Python callback handle bindings
  nb::class_<clockwork::tools::PythonCallbackHandle>(mod, "PythonCallbackHandle")
    .def_ro("sequence_number", &clockwork::tools::PythonCallbackHandle::sequence_number)
    .def_ro("message_time", &clockwork::tools::PythonCallbackHandle::message_time)
    .def_prop_ro(
      "data",
      [](const clockwork::tools::PythonCallbackHandle& obj) -> nb::bytearray
      { return nb::bytearray{obj.data_ptr, obj.data_size}; })
    .def_prop_ro(
      "data_view",
      [](const clockwork::tools::PythonCallbackHandle& obj) -> nb::ndarray<nb::numpy, const uint8_t, nb::shape<-1>>
      { return nb::ndarray<nb::numpy, const uint8_t, nb::shape<-1>>{obj.data_ptr, 1U, &obj.data_size, nb::handle{}}; })
    .def_prop_ro(
      "data",
      [](const clockwork::tools::PythonCallbackHandle& obj) -> nb::bytearray
      { return nb::bytearray{obj.data_ptr, obj.data_size}; })
    .def(
      "overrun_check",
      [](const clockwork::tools::PythonCallbackHandle& obj) -> bool { return obj.slot_ref.is_valid(); });

  // Channel spy bindings
  nb::class_<clockwork::tools::ChannelSpy>(mod, "ChannelSpy")
    .def(
      "__init__",
      [](
        clockwork::tools::ChannelSpy* ptr,
        std::string_view shm_root_dir = "/dev/shm",
        std::string_view tmp_dir = "/tmp",
        std::string_view socket_ns = "") { new (ptr) clockwork::tools::ChannelSpy{shm_root_dir, tmp_dir, socket_ns}; },
      "shm_root_dir"_a = "/dev/shm",
      "tmp_dir"_a = "/tmp",
      "socket_ns"_a = "",
      "Make a test helper.")
    .def_prop_ro(
      "channels",
      [](clockwork::tools::ChannelSpy& obj) -> std::vector<std::string>
      {
        auto channels = obj.channels();
        std::vector<std::string> channel_names;
        channel_names.reserve(channels.size());
        for (auto& channel : channels)
        {
          channel_names.emplace_back(std::move(channel.channel_name));
        }
        return channel_names;
      },
      "List the channels that are published on the local machine.")
    .def(
      "schema_definition",
      [](clockwork::tools::ChannelSpy& obj, std::string_view channel_name) -> nb::bytearray
      {
        const auto config_ptr = obj.read_channel_spy_config();
        for (const auto& channel : config_ptr->get_channels())
        {
          if (channel.get_channel_name() == channel_name)
          {
            return nb::bytearray{channel.get_schema_definition().data(), channel.get_schema_definition().size()};
          }
        }
        return nb::bytearray{};
      },
      "channel_name"_a,
      "Get the schema definition for a channel")
    .def(
      "schema_name",
      [](clockwork::tools::ChannelSpy& obj, std::string_view channel_name) -> std::string
      {
        const auto config_ptr = obj.read_channel_spy_config();
        for (const auto& channel : config_ptr->get_channels())
        {
          if (channel.get_channel_name() == channel_name)
          {
            return std::string{channel.get_schema_name()};
          }
        }
        return {};
      },
      "channel_name"_a,
      "Get the schema name for a channel")
    .def(
      "subscribe_raw_python",
      [](clockwork::tools::ChannelSpy& obj, std::string_view channel_name, clockwork::tools::PythonCallback cb_function)
      {
        const auto callback_wrapper =
          [wrapped_callback = std::move(cb_function)](const clockwork::tools::PythonCallbackHandle& callback_handle)
        {
          auto gil_acquire = nanobind::gil_scoped_acquire();
          wrapped_callback(callback_handle);
        };
        obj.subscribe(channel_name, callback_wrapper);
      },
      "channel_name"_a,
      "callback_fn"_a,
      "Subscribe with callbacks that receive raw python callback handles.")
    .def(
      "run",
      [](clockwork::tools::ChannelSpy& obj) -> void { obj.run(); },
      "Loop processing messages on subscribed channels.")
    .def(
      "run_once",
      [](clockwork::tools::ChannelSpy& obj) -> void { obj.run_once(); },
      "Process available messages on subscribed channels.");
}
