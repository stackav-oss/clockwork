# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Rules for extracting topology information from clockwork."""

load("@clockwork//clockwork:rules.bzl", "ClkInfo")

def _topology_summary_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.name + ".pkl")

    inputs = depset(transitive = [ctx.attr.system_target[ClkInfo].srcs, ctx.attr._tool[DefaultInfo].default_runfiles.files])

    repo_name = ctx.attr.system_target.label.repo_name
    if repo_name.endswith("+"):
        repo_name = repo_name[:-1]

    ctx.actions.run(
        inputs = inputs,
        outputs = [out],
        arguments = [repo_name, ctx.attr.system_target[ClkInfo].src.path, out.path],
        progress_message = "Summarizing topology of clockwork module %s" % ctx.attr.system_target[ClkInfo].src.short_path,
        mnemonic = "SummarizeClockworkTopology",
        executable = ctx.executable._tool,
        env = ctx.attr._tool[RunEnvironmentInfo].environment,
    )

    return [DefaultInfo(files = depset([out]), runfiles = ctx.runfiles(files = [out]))]

topology_summary = rule(
    implementation = _topology_summary_impl,
    attrs = {
        "system_target": attr.label(mandatory = True),
        "_tool": attr.label(
            cfg = "exec",
            default = Label("@clockwork//clockwork/tools/topology:summarize"),
            executable = True,
        ),
    },
)
