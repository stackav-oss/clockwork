# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""simplelaunch-related rules."""

_CONFIG_TOOL = "@clockwork//jewels/simplelaunch:config_tool"
_CONFIG_TEST = "@clockwork//jewels/simplelaunch:simplelaunch_config_test.sh"

def simplelaunch_config_test(name, config, **kwargs):
    """Validate a simplelaunch config.

    Args:
        name: The name of the test.
        config: The config file to validate.
        **kwargs: Extra arguments to pass to the generated py_test rule.
    """
    native.sh_test(
        name = name,
        srcs = [_CONFIG_TEST],
        data = [_CONFIG_TOOL, config],
        args = ["$(location {0})".format(_CONFIG_TOOL), "$(location {0})".format(config)],
        **kwargs
    )

def _merge_simplelaunch_config_rule_impl(ctx):
    config = ctx.actions.declare_file(ctx.attr.name)
    args = [
        "merge",
        "--output",
        config.path,
    ] + [src.path for src in ctx.files.srcs]

    ctx.actions.run(
        executable = ctx.executable._tool,
        inputs = ctx.files.srcs,
        outputs = [config],
        mnemonic = "SimpleLaunchConfig",
        arguments = args,
        progress_message = "Generating simple launch config " + str(ctx.label),
    )

    return [DefaultInfo(
        files = depset([config]),
        runfiles = ctx.runfiles(files = [config] + ctx.files.data).merge_all([src[DefaultInfo].default_runfiles for src in ctx.attr.srcs] + [data[DefaultInfo].default_runfiles for data in ctx.attr.data]),
    )]

merge_simplelaunch_config_rule = rule(
    implementation = _merge_simplelaunch_config_rule_impl,
    doc = """Generate a config for a simplelaunch.""",
    attrs = {
        "data": attr.label_list(mandatory = False, allow_files = True, cfg = "target", doc = "Extra dependencies such as message schemas"),
        "srcs": attr.label_list(mandatory = True, allow_files = [".textproto"], cfg = "target", doc = "The AppConfig files"),
        "_tool": attr.label(default = _CONFIG_TOOL, executable = True, cfg = "exec"),
    },
)

def merge_simplelaunch_config(name, **kwargs):
    merge_simplelaunch_config_rule(
        name = name,
        **kwargs
    )

    simplelaunch_config_test(
        name = name + ".test",
        config = name,
    )

_SIMPLELAUNCH_WRAPPER = """\
#!/bin/bash
exec {} {} "$@"
"""

def _simplelaunch_runner_impl(ctx):
    entrypoint = ctx.actions.declare_file(ctx.attr.name)
    ctx.actions.write(
        output = entrypoint,
        content = _SIMPLELAUNCH_WRAPPER.format(ctx.executable.simplelaunch_binary.short_path, ctx.file.config.short_path),
        is_executable = True,
    )

    return [
        DefaultInfo(
            files = depset([entrypoint]),
            runfiles = ctx.runfiles(files = [entrypoint, ctx.file.config]).merge_all(
                [
                    ctx.attr.config[DefaultInfo].default_runfiles,
                    ctx.attr.simplelaunch_binary[DefaultInfo].default_runfiles,
                ],
            ),
            executable = entrypoint,
        ),
        ctx.attr.simplelaunch_binary[RunEnvironmentInfo],
    ]

simplelaunch_runner = rule(
    implementation = _simplelaunch_runner_impl,
    doc = """Make a simplelaunch runner with the given config.""",
    attrs = {
        "config": attr.label(allow_single_file = True, doc = "The SimpleLaunch config"),
        "simplelaunch_binary": attr.label(default = ":simplelaunch", executable = True, cfg = "target"),
    },
    executable = True,
)
