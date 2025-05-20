// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/monitor_resource.hh"
#include "jewels/memory/pmr_shared_ptr.hh"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace jewels::memory
{

struct Base
{
  int a;
};
struct Derived : Base
{
  int b;
};

TEST_CASE("make_pmr_shared")
{
  MonitorResource memory;
  const jewels::memory::MemoryResource memres(&memory);

  Derived ref = {{4}, 6};
  CHECK(memory.used() == 0);
  auto derived = make_pmr_shared<Derived>(memres, ref);
  CHECK(memory.used() > 0);
  CHECK(derived->a == ref.a);
  CHECK(derived->b == ref.b);
  std::shared_ptr<Base> base = derived;
  CHECK(base->a == ref.a);
  derived.reset();
  CHECK(memory.used() > 0);
  base.reset();
  CHECK(memory.used() == 0);
}

} // namespace jewels::memory
