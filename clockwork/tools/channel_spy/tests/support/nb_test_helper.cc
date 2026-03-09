// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/repr_iface.hh"
#include "clockwork/tools/channel_spy/tests/support/test_helper.hh"
#include "clockwork/tools/channel_spy/tests/support/test_message.hh"
#include "clockwork/tools/channel_spy/tests/support/test_publisher.hh"
#include "clockwork/tools/channel_spy/tests/support/test_support.hh"
#include "jewels/container/compare.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/std/expected.hh"

#include <fmt/format.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/function.h>    // IWYU pragma: keep
#include <nanobind/stl/shared_ptr.h>  // IWYU pragma: keep
#include <nanobind/stl/string_view.h> // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// NOLINTNEXTLINE(readability-function-cognitive-complexity) Needed for nanobind
NB_MODULE(nb_test_helper, mod)
{
  namespace nb = ::nanobind;
  using TestMessageType = clockwork::Tappy<clockwork::tools::tests::support::TestMessage>;
  using TestPublisherType = clockwork::tools::tests::support::TestPublisher<TestMessageType>;
  using TestHelperType = clockwork::tools::tests::support::TestHelper<TestMessageType>;
  nb::set_leak_warnings(false);

  mod.doc() = "Test support helper nanobind wrapper";

  // Test publisher bindings
  nb::class_<TestPublisherType>(mod, "TestPublisher")
    .def(
      "publish",
      [](TestPublisherType& obj, int64_t message_time, const nb::bytearray& data) -> void
      {
        if (data.size() != sizeof(TestMessageType))
        {
          const auto msg = fmt::format(
            "Test message data size mismatch, received {}, expected {}", data.size(), sizeof(TestMessageType));
          jewels::log_cerr_error("{}", msg);
          throw std::runtime_error(msg);
        }
        if (!obj.publish(message_time, {static_cast<const std::byte*>(data.data()), data.size()}))
        {
          throw std::runtime_error("Failed to publish test message");
        }
      },
      nb::arg(),
      nb::arg());

  // Test helper bindings
  nb::class_<TestHelperType>(mod, "TestHelper")
    .def_static(
      "make_test_helper",
      [](std::string_view pinion_shm_root, std::string_view tmp_dir, std::string_view socket_ns)
        -> std::shared_ptr<TestHelperType>
      { return TestHelperType::make_test_helper(pinion_shm_root, tmp_dir, socket_ns); },
      nb::arg(),
      nb::arg(),
      nb::arg())
    .def(
      "channel_name",
      [](TestHelperType& obj, size_t channel_index) -> std::string_view { return obj.channel_name(channel_index); },
      nb::arg())
    .def(
      "open_publisher",
      [](TestHelperType& obj, size_t channel_index) -> std::shared_ptr<TestPublisherType>
      { return obj.open_publisher(channel_index); },
      nb::arg());
}
