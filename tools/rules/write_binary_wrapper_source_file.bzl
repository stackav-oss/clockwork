# Copyright 2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Rules for writing a performant wrapper script to the source tree that can be used to execute a Bazel binary target."""

load("@bazel_lib//lib:write_source_files.bzl", "write_source_file")
load("@bazel_skylib//lib:shell.bzl", "shell")
load("@rules_appimage//appimage:appimage.bzl", "appimage")

def _file_short_path(file):
    return file.short_path

def _workspace_root_relative_path(package):
    package_parts = [part for part in package.split("/") if part]
    if not package_parts:
        return "."
    return "/".join([".."] * len(package_parts))

def _bazel_bin_output_path(package, file_name):
    if package:
        return "{}/{}".format(package, file_name)
    return file_name

def _binary_wrapper_gen_impl(ctx):
    wrapped_binary = ctx.executable.wrapped_binary
    out_file = ctx.actions.declare_file(ctx.attr.out_file)
    wrapped_binary_hash_file = ctx.actions.declare_file(ctx.label.name + "_hash.txt")

    # Construct a depset of inputs that should be used to compute a hash that invalidates the cached wrapper script, including:
    # 1. all runfiles of the wrapped binary
    # 2. the binary wrapper template itself
    # 3. stable source files from the AppImage implementation
    # 4. the runtime from the appimage toolchain
    #
    # Do not include AppImage implementation runfiles here. That closure contains generated helper outputs and native
    # tools, which makes this checked-in hash sensitive to cache state and execution details that are unrelated to the
    # wrapped binary.
    wrapped_binary_runfiles = ctx.attr.wrapped_binary[DefaultInfo].default_runfiles.files
    appimage_impl_inputs = ctx.attr._appimage_impl_inputs[DefaultInfo].files
    appimage_toolchain = ctx.toolchains["@rules_appimage//appimage:appimage_toolchain_type"]
    all_inputs = depset(
        [wrapped_binary, ctx.file._template, appimage_toolchain.appimage_runtime],
        transitive = [wrapped_binary_runfiles, appimage_impl_inputs],
    )
    sorted_inputs = sorted(all_inputs.to_list(), key = _file_short_path)

    hash_inputs_file = ctx.actions.declare_file(ctx.label.name + "_hash_inputs.txt")
    ctx.actions.write(
        output = hash_inputs_file,
        content = "\n".join(["{}\t{}".format(f.short_path, f.path) for f in sorted_inputs]) + "\n",
    )

    # Compute a combined hash of all inputs excluding the appimage itself to avoid needing to build it just to check if
    # the wrapper needs to be updated.
    ctx.actions.run_shell(
        outputs = [wrapped_binary_hash_file],
        inputs = depset([hash_inputs_file], transitive = [all_inputs]),
        # Ignore files that don't exist as this can operate on symlinks that point to files that don't exist yet.
        # Hash a stable logical path with each file digest instead of hashing sha256sum's machine-local exec path.
        command = """while IFS="$(printf '\\t')" read -r logical_path exec_path; do
  if digest_line="$(sha256sum "${{exec_path}}" 2>/dev/null)"; then
    file_hash="${{digest_line%% *}}"
    printf "%s  %s\\n" "${{file_hash}}" "${{logical_path}}"
  fi
done < {inputs} | sha256sum | cut -d ' ' -f 1 > {output}""".format(
            inputs = hash_inputs_file.path,
            output = wrapped_binary_hash_file.path,
        ),
        mnemonic = "ComputeInputsHash",
        use_default_shell_env = True,
    )

    # Partially expand the wrapper script template
    template = ctx.file._template
    partial_wrapper = ctx.actions.declare_file(ctx.label.name + "_partial.sh")
    ctx.actions.expand_template(
        template = template,
        output = partial_wrapper,
        substitutions = {
            "{{ appimage_output_path }}": ctx.attr.appimage_output_path,
            "{{ appimage_target }}": ctx.attr.appimage_label,
            "{{ bazel_build_flags_env_var_name }}": ctx.attr.bazel_build_flags_env_var_name or "",
            "{{ bazel_startup_flags }}": " ".join([shell.quote(flag) for flag in ctx.attr.bazel_startup_flags]),
            "{{ bazel_startup_flags_env_var_name }}": ctx.attr.bazel_startup_flags_env_var_name or "",
            "{{ update_target }}": ctx.attr.update_target_label,
            "{{ workspace_root_relative_path }}": _workspace_root_relative_path(ctx.label.package),
        },
    )

    # Finalize the wrapper by injecting the hash
    ctx.actions.run_shell(
        inputs = [partial_wrapper, wrapped_binary_hash_file],
        outputs = [out_file],
        command = 'sed "s|{{ expected_hash }}|$(cat %s)|g" %s > %s' % (wrapped_binary_hash_file.path, partial_wrapper.path, out_file.path),
        mnemonic = "FinalizeWrapper",
    )

    return [DefaultInfo(files = depset([out_file]), executable = out_file)]

_binary_wrapper_gen = rule(
    implementation = _binary_wrapper_gen_impl,
    attrs = {
        # This is a string instead of a label to avoid making this target that updates the wrapper script require
        # building the appimage
        "appimage_label": attr.string(mandatory = True),
        "appimage_output_path": attr.string(mandatory = True),
        "bazel_build_flags_env_var_name": attr.string(mandatory = False),
        "bazel_startup_flags": attr.string_list(mandatory = False),
        "bazel_startup_flags_env_var_name": attr.string(mandatory = False),
        "out_file": attr.string(mandatory = True),
        "update_target_label": attr.string(mandatory = True),
        "wrapped_binary": attr.label(mandatory = True, executable = True, cfg = "target"),
        "_appimage_impl_inputs": attr.label(default = "@rules_appimage//appimage:appimage_impl_hash_inputs"),
        "_template": attr.label(default = "//tools/rules:resources/bazel_binary_wrapper.sh.j2", allow_single_file = True),
    },
    executable = True,
    toolchains = [
        # So that we can invalidate the cache if the appimage runtime changes
        "@rules_appimage//appimage:appimage_toolchain_type",
    ],
)

def _write_binary_wrapper_source_file_impl(name, visibility, binary, bazel_build_flags_env_var_name, bazel_startup_flags, bazel_startup_flags_env_var_name, out_file):
    generated_appimage_label = name + "_generated_appimage"

    appimage(
        name = generated_appimage_label,
        binary = binary,
        preserve_working_directory = True,
        visibility = ["//visibility:private"],
        tags = ["manual"],
    )

    generated_wrapper_script_label = name + "_generated_wrapper_script"

    _binary_wrapper_gen(
        name = generated_wrapper_script_label,
        bazel_build_flags_env_var_name = bazel_build_flags_env_var_name,
        bazel_startup_flags = bazel_startup_flags,
        bazel_startup_flags_env_var_name = bazel_startup_flags_env_var_name,
        appimage_label = str(native.package_relative_label(generated_appimage_label)),
        appimage_output_path = _bazel_bin_output_path(native.package_name(), generated_appimage_label),
        wrapped_binary = binary,
        out_file = name + "_generated.sh",
        update_target_label = str(native.package_relative_label(name)),
        visibility = ["//visibility:private"],
    )

    write_source_file(
        name = name,
        executable = True,
        in_file = generated_wrapper_script_label,
        out_file = out_file,
        check_that_out_file_exists = False,
        visibility = visibility,
        # Hashes and paths are different inside and outside the workspace so only add this test if we're in the workspace.
        tags = None if native.repo_name() == "" else ["manual"],
    )

write_binary_wrapper_source_file = macro(
    implementation = _write_binary_wrapper_source_file_impl,
    attrs = {
        "bazel_build_flags_env_var_name": attr.string(mandatory = False, doc = "Optional environment variable name that the wrapper script will read for Bazel build command flags to use when building the binary"),
        "bazel_startup_flags": attr.string_list(mandatory = False, doc = "Optional Bazel startup flags to use when building the binary"),
        "bazel_startup_flags_env_var_name": attr.string(mandatory = False, doc = "Optional environment variable name that overrides the Bazel startup flags when building the binary"),
        "binary": attr.label(mandatory = True, doc = "The binary target to wrap"),
        "out_file": attr.string(mandatory = True, configurable = False, doc = "The name of the file that should be written to the source tree relative to the current package"),
    },
)
