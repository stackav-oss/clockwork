# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Wrappers for cc_* rules."""

load(
    "@rules_cc//cc:defs.bzl",
    _CcInfo = "CcInfo",
    _CcToolchainConfigInfo = "CcToolchainConfigInfo",
    _DebugPackageInfo = "DebugPackageInfo",
    _cc_binary = "cc_binary",
    _cc_common = "cc_common",
    _cc_import = "cc_import",
    _cc_library = "cc_library",
    _cc_shared_library = "cc_shared_library",
    _cc_test = "cc_test",
    _cc_toolchain = "cc_toolchain",
    _cc_toolchain_suite = "cc_toolchain_suite",
)

# TODO(OI-3126): De-duplicate rule definitions.

def cc_binary(name, deps = None, **kwargs):
    """Wrapper for cc_binary.

    Adds functionality to print a stacktrace to stderr during segfault or abort.

    Listed args are modified for our use. Others are passed through as-is.

    Args:
        name: Here to make lint happy.
        deps: as cc_binary.
        **kwargs: passthrough to underlying cc_binary.
    """
    deps = (deps or []) + [_stacktrace_label]
    _cc_binary(name = name, deps = deps, **kwargs)

def cc_test(name, deps = None, **kwargs):
    deps = (deps or [])
    if (
        # This is a macro, so deps may not be resolved to a list
        # yet. If it's not a list, we can't check if we've already
        # depended on it. Error on the side of adding it, since we
        # very rarely already depend on it.
        type(deps) != "list" or
        (_stacktrace_label not in deps and
         # Also check without the @@ prefix.
         _stacktrace_label[2:] not in deps)
    ):
        # Note that we do not use append() because deps might not
        # actually be a list in the case that it's a select
        # expression.
        deps += [_stacktrace_label]  # buildifier: disable=list-append

    _cc_test(name = name, deps = deps, **kwargs)

# Pass through to rules_cc.
# keep-sorted start
CcInfo = _CcInfo
CcToolchainConfigInfo = _CcToolchainConfigInfo
DebugPackageInfo = _DebugPackageInfo
cc_common = _cc_common
cc_import = _cc_import
cc_library = _cc_library
cc_shared_library = _cc_shared_library
cc_toolchain = _cc_toolchain
cc_toolchain_suite = _cc_toolchain_suite
# keep-sorted end

_stacktrace_label = "@@//tools/cc:stacktrace"
