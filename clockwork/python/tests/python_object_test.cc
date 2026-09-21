// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/gil_lock_guard.hh"
#include "clockwork/python/python_init.hh"
#include "clockwork/python/python_object.hh"
#include "jewels/memory/memory_resource.hh"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <memory_resource>
#include <string>
#include <utility>

namespace clockwork::python::tests
{
namespace
{

TEST_CASE("Python object test")
{
  // Repeat this test case to catch reference counting bugs
  const auto test_index = GENERATE(range(0, 1000));
  CAPTURE(test_index);

  const jewels::memory::MemoryResource memory_resource(std::pmr::new_delete_resource());
  python_init(InitializationMode::unit_test);
  const GilLockGuard gil_guard;

  const auto helper_module_dict =
    PythonObject::import_module("clockwork.python.tests.support.python_object_test_helper");
  const auto helper_class = helper_module_dict.get_dictionary_item("TestHelper");
  const auto helper_obj = helper_class.call_object();

  SECTION("lifecycle")
  {
    PythonObject obj1;
    REQUIRE_FALSE(obj1);
    REQUIRE(obj1.get_ptr() == nullptr);
    REQUIRE_THROWS(obj1.get_refcnt());

    auto obj2 = PythonObject::make_string("Test1");
    REQUIRE(obj2);
    REQUIRE(obj2.get_ptr() != nullptr);
    REQUIRE(obj2.get_refcnt() == 1U);
    REQUIRE(obj2.get_string() == "Test1");

    obj1 = obj2;
    REQUIRE(obj1);
    REQUIRE(obj1.get_ptr() == obj2.get_ptr());
    REQUIRE(obj1.get_refcnt() == 2U);
    REQUIRE(obj2.get_refcnt() == 2U);

    obj1 = PythonObject::make_string("Test2");
    REQUIRE(obj1);
    REQUIRE(obj1.get_ptr() != nullptr);
    REQUIRE(obj1.get_refcnt() == 1U);
    REQUIRE(obj2.get_refcnt() == 1U);
    REQUIRE(obj1.get_string() == "Test2");

    obj2 = std::move(obj1);
    REQUIRE(obj2);
    REQUIRE(obj2.get_ptr() != nullptr);
    REQUIRE(obj2.get_refcnt() == 1U);
    REQUIRE(obj2.get_string() == "Test2");

    obj2.reset();
    REQUIRE_FALSE(obj2);
  }

  SECTION("get python error")
  {
    const auto str_obj = PythonObject::make_string("Input string");
    REQUIRE_THROWS_WITH(str_obj.get_integer(), "Internal python error");
  }

  SECTION("make/get python string")
  {
    const auto input_str = PythonObject::make_string("Input string");
    const auto output_str = helper_obj.call_method("echo_string", input_str);
    REQUIRE(output_str.get_string() == "Input string");
  }

  SECTION("make/get python integer")
  {
    const auto lhs_int = PythonObject::make_integer(3);
    const auto rhs_int = PythonObject::make_integer(5);
    const auto sum_obj = helper_obj.call_method("add_numbers", lhs_int, rhs_int);
    REQUIRE(sum_obj.get_integer() == 8);
  }

  SECTION("make memory view")
  {
    const std::array<char, 8> input{'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    std::array<char, 8> output{};
    const auto input_obj = PythonObject::make_read_only_memory_view(input.data(), input.size());
    const auto output_obj = PythonObject::make_writable_memory_view(output.data(), output.size());
    const auto result = helper_obj.call_method("echo_memoryview", input_obj, output_obj);
    REQUIRE(input == output);
    REQUIRE(result.is_none());
  }

  SECTION("python tuple")
  {
    const auto tuple_obj = PythonObject::make_tuple(PythonObject::make_integer(11), PythonObject::make_string("Test"));
    REQUIRE(tuple_obj.get_tuple_size() == 2U);
    REQUIRE(tuple_obj.get_tuple_element(0U).get_integer() == 11);
    REQUIRE(tuple_obj.get_tuple_element(1U).get_string() == "Test");
  }

  SECTION("python list")
  {
    const auto list_obj =
      PythonObject::make_list(std::array{PythonObject::make_integer(11), PythonObject::make_string("Test")});
    REQUIRE(list_obj.get_list_size() == 2U);
    REQUIRE(list_obj.get_list_element(0U).get_integer() == 11);
    REQUIRE(list_obj.get_list_element(1U).get_string() == "Test");
  }

  SECTION("python dictionary")
  {
    const auto dict_obj = PythonObject::make_dictionary();
    REQUIRE(dict_obj.get_dictionary_size() == 0U);
    REQUIRE_FALSE(dict_obj.get_dictionary_item("Item1"));
    dict_obj.set_dictionary_item("Item1", PythonObject::make_string("Test"));
    dict_obj.set_dictionary_item("Item2", PythonObject::make_true());
    dict_obj.set_dictionary_item("Item3", PythonObject::make_false());
    REQUIRE(dict_obj.get_dictionary_size() == 3U);
    REQUIRE(dict_obj.get_dictionary_item("Item1").get_string() == "Test");
    REQUIRE(dict_obj.get_dictionary_item("Item2").is_true());
    REQUIRE(dict_obj.get_dictionary_item("Item3").is_false());
  }
}

} // namespace
} // namespace clockwork::python::tests
