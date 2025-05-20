# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Flags for our cc toolchain."""

_COMMON_WARNING_FLAGS = [
    "-pedantic",
    "-Wall",
    "-Wchar-subscripts",
    "-Wconversion",
    "-Werror",
    "-Wextra",
    "-Wimplicit-fallthrough",
    "-Wnarrowing",
    "-Wsign-conversion",
    "-Wshadow",
]

# C flags common between Clang and GCC
_COMMON_C_WARNING_FLAGS = _COMMON_WARNING_FLAGS + [
    "-Wbad-function-cast",
]

# C++ flags common between Clang and GCC
_COMMON_CXX_WARNING_FLAGS = _COMMON_WARNING_FLAGS + [
    "-Wnon-virtual-dtor",
    "-Wdelete-non-virtual-dtor",
]

CLANG_C_WARNING_FLAGS = _COMMON_C_WARNING_FLAGS + [
    "-Warray-bounds-pointer-arithmetic",
    "-Wcomma",
    "-Wconditional-uninitialized",
    "-Wdocumentation",
    "-Wdocumentation-pedantic",
    "-Widiomatic-parentheses",
    "-Wimplicit-float-conversion",
    "-Wnewline-eof",
    "-Wself-assign",
    "-Wshadow-all",
]

CLANG_CXX_WARNING_FLAGS = _COMMON_CXX_WARNING_FLAGS + [
    "-Wdelete-non-virtual-dtor",
    "-Wnon-virtual-dtor",
    "-Wold-style-cast",
    "-Woverriding-method-mismatch",
    "-Wreorder-ctor",
]

CLANG_MISC_FLAGS = [
    "-no-canonical-prefixes",
    "-fcolor-diagnostics",
    "-fbracket-depth=1024",
]

GCC_C_WARNING_FLAGS = _COMMON_C_WARNING_FLAGS

GCC_CXX_WARNING_FLAGS = _COMMON_CXX_WARNING_FLAGS + [
    "-Wno-psabi",
]

GCC_MISC_FLAGS = [
    # Explicitly disable __cpp_impl_three_way_comparison to support Clang-based tooling (clangd, IWYU, Clang-Tidy, etc)
    # because the GCC 9.3 STL doesn't have <compare>
    "-U__cpp_impl_three_way_comparison",
    "-fdiagnostics-color=always",
]

# Flags specific to X86_64 machines
X86_FLAGS = ["-march=skylake"]

# Flags specific to Aarch64 machines
AARCH_FLAGS = ["-march=armv8.2-a+fp16+simd+dotprod+ssbs"]
