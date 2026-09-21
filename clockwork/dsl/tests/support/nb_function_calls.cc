// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dsl/tests/support/nanobindable_messages.hh"
#include "clockwork/repr_iface.hh"

#include <nanobind/nanobind.h>

using Foo = clockwork::Tappy<::foo::Foo>;
using SubMessage = clockwork::Tappy<::foo::SubMessage>;

Foo make_foo()
{
  Foo foo;
  foo.get_mutable_msg().set_int_field(100);
  foo.set_prim(101);
  return foo;
}

void mutate_foo(Foo& foo)
{
  foo.get_mutable_msg().set_int_field(22);
  foo.set_prim(42);
}

NB_MODULE(nb_function_calls, py_module)
{
  namespace nb = nanobind;

  nb::module_::import_("clockwork.dsl.tests.support.nanobindable_messages_clk_nb");

  py_module.def("make_foo", &make_foo);
  py_module.def("mutate_foo", &mutate_foo);
}
