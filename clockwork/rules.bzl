# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Definition of custom Clockwork compilation rule."""

# Normally, getting the files attribute of your deps includes all files.  When a
# .clk file depends on other .clk files, we really only need the srcs, so we
# define a custom provider for this.

load("@aspect_bazel_lib//lib:write_source_files.bzl", "write_source_file")

ClkInfo = provider("Collects Clockwork source files", fields = ["src", "srcs", "cache"])

def _clk_impl(ctx):
    if len(ctx.files.srcs) != 1:
        error = "Must provide exactly one .clk file for srcs {}".format([s.path for s in ctx.files.srcs])
        fail(error)
    src = ctx.files.srcs[0]
    srcs = depset([src], transitive = [dep[ClkInfo].srcs for dep in ctx.attr.deps if ClkInfo in dep])

    input_dep_cache = depset([], transitive = [dep[ClkInfo].cache for dep in ctx.attr.deps if ClkInfo in dep])
    cache_files = []
    if ctx.attr.compile:
        pkl_file = ctx.actions.declare_file(src.basename + "_pkl")
        cache_files.append(pkl_file)

        args = ctx.actions.args()
        args.add("compile-module")
        args.add("--input")
        args.add(src.path)
        args.add("--root")
        args.add(pkl_file.root.path)

        args.add("--repo")
        args.add(ctx.attr.repo)

        if ctx.attr.write_json_files:
            args.add("--write-json-files")

        ctx.actions.run(
            inputs = depset(transitive = [srcs, ctx.attr._clkc[DefaultInfo].default_runfiles.files, input_dep_cache]),
            outputs = ctx.outputs.outs + [pkl_file],
            arguments = [args],
            progress_message = "Compiling Clockwork module %s" % ctx.files.srcs[0].short_path,
            mnemonic = "CompileClockworkModule",
            executable = ctx.executable._clkc,
            env = ctx.attr._clkc[RunEnvironmentInfo].environment,
        )

    new_cache = depset(cache_files, transitive = [input_dep_cache])

    files = depset(direct = ctx.outputs.outs + [src] + cache_files, transitive = [srcs, ctx.attr._clkc[DefaultInfo].files])
    runfiles = ctx.runfiles(files = ctx.outputs.outs + [src] + cache_files).merge_all([dep[DefaultInfo].default_runfiles for dep in ctx.attr.deps])
    return [
        DefaultInfo(files = files, runfiles = runfiles),
        ClkInfo(src = src, srcs = srcs, cache = new_cache),
        OutputGroupInfo(clk_files = srcs),
    ]

_clk = rule(
    implementation = _clk_impl,
    attrs = {
        "compile": attr.bool(default = True),
        "deps": attr.label_list(),
        "outs": attr.output_list(),
        "repo": attr.string(mandatory = True),
        "srcs": attr.label_list(allow_files = [".clk"]),
        "write_json_files": attr.bool(default = False),
        "_clkc": attr.label(
            cfg = "exec",
            default = Label("//clockwork/dsl:clkc"),
            executable = True,
        ),
    },
)

def _collect_clk_files_impl(ctx):
    clk_deps = ctx.attr.clk_target[ClkInfo].srcs
    return DefaultInfo(files = clk_deps, runfiles = ctx.runfiles(clk_deps.to_list()))

_collect_clk_files = rule(
    implementation = _collect_clk_files_impl,
    attrs = {"clk_target": attr.label()},
)

def _update_clk_targets(name, srcs):
    #  The rule guarantees that srcs is exactly one item.
    clk_file = srcs[0]

    _collect_clk_files(
        name = name + ".clk_files",
        clk_target = name,
    )
    native.genrule(
        name = name + ".build_gen",
        srcs = [
            clk_file,
            name + ".clk_files",
            "BUILD.bazel",
        ],
        outs = ["BUILD." + name + ".bazel"],
        cmd = "$(location @clockwork//clockwork/dsl/bazel:update_clk_targets) $(location @buildifier_prebuilt//:buildifier) $(location @buildifier_prebuilt//:buildozer) $(location BUILD.bazel) $(location " + clk_file + ") $@ --repo " + native.module_name(),
        tools = [
            "@clockwork//clockwork/dsl/bazel:update_clk_targets",
            "@buildifier_prebuilt//:buildifier",
            "@buildifier_prebuilt//:buildozer",
        ],
        tags = ["no-remote-exec", "clk-deps"],
    )
    write_source_file(
        name = name + ".build",
        in_file = "BUILD." + name + ".bazel",
        out_file = "BUILD.bazel",
        tags = ["clk-deps"],
        #TODO(OI-3066) Resolve write_source_file issue across multiple repos.
        diff_test = True,
    )

def clk(name, srcs, compile = True, write_json_files = False, **kwargs):
    """Compile a clk file.

    Args:
        name: The name of the clk target.
        srcs: A list containing a single .clk file.
        compile: Whether to run the compiler or not (False essentially turns this into a filegroup).
        write_json_files: Whether to write JSON versions of the config or not.
        **kwargs: Additional arguments passed through to the underlying clk rule.
    """

    _clk(name = name, srcs = srcs, repo = native.module_name(), compile = compile, write_json_files = write_json_files, **kwargs)

    # Skip this step for externals.
    if native.repo_name() == "" and compile:
        _update_clk_targets(name, srcs)
