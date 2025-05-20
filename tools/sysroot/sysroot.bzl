# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Macro to make a sysroot with clang tools."""

load("//tools/sysroot:clang.bzl", "make_clang_targets")

def make_sysroot(name):
    """Make targets for a clang sysroot."""
    make_clang_targets(name = "clang_targets")
