# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Shim to run the real tests, which live in 'nb_get_python_type_name_test.cc'."""

from types import ModuleType

import jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test  # this line is where the tests actually run


def test_import() -> None:
    assert isinstance(jewels.nanobind.clk_bindings.tests.support.nb_get_python_type_name_test, ModuleType)
