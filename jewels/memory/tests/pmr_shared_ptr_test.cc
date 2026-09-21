// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/callsig/outparam.hh"
#include "jewels/memory/instrumented_pmr_resource.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/new_delete_memory_resource.hh"
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
  NewDeleteMemoryResource memory(0, "pmr_shared_ptr_test_memres");
  const jewels::memory::MemoryResource memres{memory};
  MemoryResourceMetrics metrics{};

  Derived ref = {{4}, 6};
  memory.get_memory_resource_metrics(jewels::Out{metrics});
  CHECK(metrics.current_allocated == 0);
  auto derived = make_pmr_shared<Derived>(memres, ref);
  memory.get_memory_resource_metrics(jewels::Out{metrics});
  CHECK(metrics.current_allocated > 0);
  CHECK(derived->a == ref.a);
  CHECK(derived->b == ref.b);
  std::shared_ptr<Base> base = derived;
  CHECK(base->a == ref.a);
  derived.reset();
  memory.get_memory_resource_metrics(jewels::Out{metrics});
  CHECK(metrics.current_allocated > 0);
  base.reset();
  memory.get_memory_resource_metrics(jewels::Out{metrics});
  CHECK(metrics.current_allocated == 0);
}

} // namespace jewels::memory
