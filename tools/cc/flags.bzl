# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Flags for our cc toolchain."""

_COMMON_WARNING_FLAGS = [
    # keep-sorted start
    "-Wall",
    "-Wchar-subscripts",
    "-Wconversion",
    "-Werror",
    "-Wextra",
    "-Wimplicit-fallthrough",
    "-Wnarrowing",
    "-Wshadow",
    "-Wsign-conversion",
    "-Wunreachable-code",
    "-pedantic",
    # keep-sorted end
]

# C flags common between Clang and GCC
_COMMON_C_WARNING_FLAGS = _COMMON_WARNING_FLAGS + [
    # keep-sorted start
    "-Wbad-function-cast",
    # keep-sorted end
]

# C++ flags common between Clang and GCC
_COMMON_CXX_WARNING_FLAGS = _COMMON_WARNING_FLAGS + [
    # keep-sorted start
    "-Wdelete-non-virtual-dtor",
    "-Wnon-virtual-dtor",
    # keep-sorted end
]

CLANG_C_WARNING_FLAGS = _COMMON_C_WARNING_FLAGS + [
    # keep-sorted start
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
    # keep-sorted end
]

CLANG_CXX_WARNING_FLAGS = _COMMON_CXX_WARNING_FLAGS + [
    # keep-sorted start
    "-Wdelete-non-virtual-dtor",
    "-Wnon-virtual-dtor",
    "-Wold-style-cast",
    "-Woverriding-method-mismatch",
    "-Wreorder-ctor",
    # keep-sorted end
]

CLANG_MISC_FLAGS = [
    # keep-sorted start
    "-fbracket-depth=1024",
    "-fcolor-diagnostics",
    "-no-canonical-prefixes",
    # keep-sorted end
]

GCC_C_WARNING_FLAGS = _COMMON_C_WARNING_FLAGS

GCC_CXX_WARNING_FLAGS = _COMMON_CXX_WARNING_FLAGS + [
    # keep-sorted start
    "-Wno-psabi",
    # keep-sorted end
]

GCC_MISC_FLAGS = [
    # keep-sorted start
    # Explicitly disable __cpp_impl_three_way_comparison to support Clang-based tooling (clangd, IWYU, Clang-Tidy, etc)
    # because the GCC 9.3 STL doesn't have <compare>
    "-U__cpp_impl_three_way_comparison",
    "-fdiagnostics-color=always",
    # keep-sorted end
]

# Flags specific to X86_64 machines
X86_FLAGS = [
    # keep-sorted start
    "-march=skylake",
    # keep-sorted end
]

# Flags specific to Aarch64 machines
AARCH_FLAGS = [
    # keep-sorted start
    "-march=armv8.2-a+fp16+simd+dotprod+ssbs",
    # keep-sorted end
]
