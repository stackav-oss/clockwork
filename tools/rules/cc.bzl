# Copyright 2025-2026 Stack AV Co.
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
    """Repo-wide wrapper for cc_binary.

    Used when we need to provide things on a per-binary basis rather than globally.

    Listed args are modified for our use. Others are passed through as-is.

    Args:
        name: Here to make lint happy.
        deps: as cc_binary.
        **kwargs: passthrough to underlying cc_binary.
    """

    # Create a base version of the binary and then use the split output rule to generate the final binary. This
    # ensures proper handling of debug symbols.
    _cc_binary(
        name = name + "_base",
        deps = (deps or []) + [_stacktrace_label],
        **kwargs
    )
    _cc_binary_split_output(
        name = name,
        binary = name + "_base",
        visibility = kwargs.get("visibility", None),
        linkshared = kwargs.get("linkshared", None),
        testonly = kwargs.get("testonly", None),
    )

def _cc_binary_split_output_impl(ctx):
    binary = ctx.attr.binary
    default_info = binary[DefaultInfo]
    input_executable = default_info.files_to_run.executable
    target_name = ctx.label.name
    output_executable = ctx.actions.declare_file(target_name)
    llvm_objcopy = ctx.executable._llvm_objcopy
    llvm_strip = ctx.executable._llvm_strip

    outputs = [output_executable]
    command = """
        {objcopy} {original} {target} && \
        {strip} --strip-debug --strip-unneeded {target}""".format(
        objcopy = llvm_objcopy.path,
        strip = llvm_strip.path,
        original = input_executable.path,
        target = output_executable.path,
    )
    mnemonic = "StripDebugInfo"

    ctx.actions.run_shell(
        inputs = depset(
            [input_executable, llvm_objcopy, llvm_strip],
            transitive = [
                ctx.attr._llvm_objcopy[DefaultInfo].default_runfiles.files,
                ctx.attr._llvm_strip[DefaultInfo].default_runfiles.files,
            ],
        ),
        outputs = outputs,
        command = command,
        mnemonic = mnemonic,
    )

    # Filter out the base file from runfiles to ensure it is not included in the final output
    base_runfiles = default_info.data_runfiles.files.to_list()
    label_name = binary.label.name
    if ctx.attr.linkshared:
        label_name = "lib" + label_name + ".so"
    filtered_runfiles = [
        f
        for f in base_runfiles
        if not f.basename == label_name
    ]
    runfiles = ctx.runfiles(files = filtered_runfiles)

    run_env_info = RunEnvironmentInfo()
    if RunEnvironmentInfo in binary:
        run_env_info = binary[RunEnvironmentInfo]

    ret_val = [DefaultInfo(
        files = depset([output_executable]),
        runfiles = runfiles,
        executable = output_executable,
    ), run_env_info]

    ccinfo = binary[CcInfo]
    if ccinfo:
        ret_val.append(cc_common.merge_cc_infos(cc_infos = [ccinfo]))

    return ret_val

_cc_binary_split_output = rule(
    implementation = _cc_binary_split_output_impl,
    attrs = {
        "binary": attr.label(allow_single_file = True),
        "deps": attr.label_list(),
        "linkshared": attr.bool(default = False),
        "_llvm_objcopy": attr.label(
            default = Label("@clang//:llvm_objcopy"),
            executable = True,
            cfg = "exec",
            allow_files = True,
        ),
        "_llvm_strip": attr.label(
            default = Label("@clang//:llvm_strip"),
            executable = True,
            cfg = "exec",
            allow_files = True,
        ),
    },
    executable = True,
    doc = """This rule processes a precompiled C++ binary by separating its debug information from the executable. It \
          outputs a debug-only binary for troubleshooting and a stripped binary for production use, ensuring a lean \
          runtime artifact while preserving full debugging capabilities when needed.""",
)

def cc_test(name, deps = None, data = None, env = None, **kwargs):
    """Repo-wide wrapper for cc_test.

    Listed args are modified for our use. Others are passed through as-is.

    Args:
        name: Here to make lint happy.
        deps: as cc_test.
        data: as cc_test.
        env: as cc_test.
        **kwargs: passthrough to underlying cc_test.
    """
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
    data = (data or []) + ["@clang//:llvm_symbolizer"] + select({
        "@@//tools/cc/sanitizer:aubsan": [
            "@@//tools/cc/sanitizer:asan_test_suppressions.txt",
            "@@//tools/cc/sanitizer:lsan_test_suppressions.txt",
            "@@//tools/cc/sanitizer:ubsan_test_suppressions.txt",
        ],
        "@@//tools/cc/sanitizer:tsan": ["@@//tools/cc/sanitizer:tsan_test_suppressions.txt"],
        "//conditions:default": [],
    })
    env = {
        "ASAN_SYMBOLIZER_PATH": "$(location @clang//:llvm_symbolizer)",
        "LSAN_SYMBOLIZER_PATH": "$(location @clang//:llvm_symbolizer)",
        "TSAN_SYMBOLIZER_PATH": "$(location @clang//:llvm_symbolizer)",
        "UBSAN_SYMBOLIZER_PATH": "$(location @clang//:llvm_symbolizer)",
    } | (env or {})

    _cc_test(name = name, data = data, deps = deps, env = env, **kwargs)

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
