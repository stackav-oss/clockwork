// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/device_ptr.hh"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil/catch2.hpp> // IWYU pragma: keep (registers catch2 as the error handler)
#include <trompeloeil/mock.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace clockwork::pinion
{
namespace
{

class MockDevicePtrFactory : public DevicePtrFactory
{
public:
  MAKE_MOCK2(make_device_ptr, (DevicePtr<void>)(size_t, void*), override);
  MAKE_MOCK2(make_device_ptr, (DevicePtr<const void>)(size_t, const void*), override);
  MAKE_MOCK3(release_device_ptr, (void)(size_t, void*, void*), override);
  MAKE_MOCK3(release_device_ptr, (void)(size_t, const void*, const void*), override);
};

TEST_CASE("DevicePtr default construction")
{
  const DevicePtr<int> ptr;

  REQUIRE_FALSE(ptr);
  REQUIRE(ptr.get() == nullptr);
  REQUIRE(ptr.operator->() == nullptr);
}

TEST_CASE("DevicePtr releases its device pointer")
{
  MockDevicePtrFactory factory;
  uint32_t cpu_data{};
  uint32_t device_data{};

  REQUIRE_CALL(factory, release_device_ptr(sizeof(cpu_data), &cpu_data, &device_data));
  {
    const DevicePtr<uint32_t> ptr{sizeof(cpu_data), &cpu_data, &device_data, &factory};
    REQUIRE(ptr);
    REQUIRE(ptr.get() == &device_data);
    REQUIRE(ptr.operator->() == &device_data);
  }
}

TEST_CASE("DevicePtr move construction transfers ownership")
{
  MockDevicePtrFactory factory;
  uint32_t cpu_data{};
  uint32_t device_data{};

  REQUIRE_CALL(factory, release_device_ptr(sizeof(cpu_data), static_cast<const void*>(&cpu_data), &device_data));
  DevicePtr<uint32_t> source{sizeof(cpu_data), &cpu_data, &device_data, &factory};
  DevicePtr<const uint32_t> destination{std::move(source)};

  REQUIRE_FALSE(source); // NOLINT(bugprone-use-after-move) testing move behavior
  REQUIRE(source.get() == nullptr);
  REQUIRE(destination);
  REQUIRE(destination.get() == &device_data);
  STATIC_REQUIRE(std::is_same_v<decltype(destination.get()), const uint32_t*>);
}

TEST_CASE("DevicePtr move assignment releases the previous destination")
{
  MockDevicePtrFactory factory;
  uint32_t src_cpu_data{};
  uint32_t dst_cpu_data{};
  uint32_t src_device_data{};
  uint32_t dst_device_data{};

  REQUIRE_CALL(factory, release_device_ptr(sizeof(dst_cpu_data), &dst_cpu_data, &dst_device_data));
  REQUIRE_CALL(factory, release_device_ptr(sizeof(src_cpu_data), &src_cpu_data, &src_device_data));
  DevicePtr<uint32_t> source{sizeof(src_cpu_data), &src_cpu_data, &src_device_data, &factory};
  DevicePtr<uint32_t> destination{sizeof(dst_cpu_data), &dst_cpu_data, &dst_device_data, &factory};

  destination = std::move(source);

  REQUIRE_FALSE(source); // NOLINT(bugprone-use-after-move) testing move behavior
  REQUIRE(destination);
  REQUIRE(destination.get() == &src_device_data);
}

TEST_CASE("DevicePtr reinterpret pointer cast transfers ownership")
{
  MockDevicePtrFactory factory;
  uint32_t value{};
  uint32_t device_data{};

  REQUIRE_CALL(factory, release_device_ptr(sizeof(value), &value, &device_data));
  DevicePtr<uint32_t> source{sizeof(value), &value, &device_data, &factory};
  auto destination = reinterpret_pointer_cast<std::byte>(std::move(source));

  REQUIRE_FALSE(source); // NOLINT(bugprone-use-after-move) testing move behavior
  REQUIRE(destination);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) testing cast
  REQUIRE(destination.get() == reinterpret_cast<std::byte*>(&device_data));
  STATIC_REQUIRE(std::is_same_v<decltype(destination.get()), std::byte*>);
}

} // namespace
} // namespace clockwork::pinion
