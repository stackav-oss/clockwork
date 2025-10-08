# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Macros to support writing Python requirements files."""

load("@aspect_bazel_lib//lib:write_source_files.bzl", "write_source_file")

def write_python_requirements_source_file(
        name,
        requirements_in,
        overrides_in,
        requirements_out,
        existing_requirements_out = None,
        strip_extras = False,
        emit_index_urls = True,
        generate_hashes = True,
        visibility = None,
        python_version = "3.10"):
    """Write a Python requirements source file using `uv`.

    Args:
          name: Name of target that should be run to update and rewrite the requirements source file.
          requirements_in: Label of file containing input requirements.
          overrides_in: Label of file containing overrides for input requirements and their transitive dependencies.
          requirements_out: Name of source file to generate containing updated locked requirements.
          existing_requirements_out: Optional label of file containing existing locked requirements.
            If None, requirements_out is used.
          strip_extras: Whether or not to remove extras in requirements_out.
          emit_index_urls: Whether or not to include `--index-url` and `--extra-index-url` entries in requirements_out.
          generate_hashes: Whether or not to include hashes in the generated requirements file.
          visibility: Visibility, forwarded to the write_source_file macro
          python_version: Target Python version for the `uv` command.

    Returns:
        The name of the generated intermediate requirements file.
    """
    generator_name = name + "_generator"
    generated_requirements_out = name + "_generated.txt"
    existing_requirements = existing_requirements_out or requirements_out

    native.genrule(
        name = generator_name,
        srcs = [
            overrides_in,
            requirements_in,
            existing_requirements,
        ],
        outs = [generated_requirements_out],
        cmd = " ".join([
            "timeout",
            "30m",
            "$(execpath //tools/language_support:uv_wrapper)",
            "--python-version={}".format(python_version),
            "--strip-extras" if strip_extras else "--no-strip-extras",
            "--emit-index-url" if emit_index_urls else "--no-emit-index-url",
            "--generate-hashes" if generate_hashes else "--no-generate-hashes",
            "$(execpath {})".format(requirements_in),
            "$(execpath {})".format(existing_requirements),
            "$(execpath {})".format(overrides_in),
            "$(execpath //tools/language_support:uv_bin)",
            "$(execpath //tools/language_support:uv_toml)",
            "$@",
        ]),
        tags = [
            "no-remote-exec",
            "no-sandbox",
        ],
        tools = ["//tools/language_support:uv_wrapper", "//tools/language_support:uv_toml", "//tools/language_support:uv_bin"],
    )

    write_source_file(
        name = name,
        in_file = generated_requirements_out,
        out_file = requirements_out,
        visibility = visibility,
        # Only use a diff_test if the current repo is the root. Diff tests cannot work cross-repository.
        # repo_name() is the empty string if this is the root repository. It is non-empty if it is non-root.
        diff_test = not native.repo_name(),
    )

    return generated_requirements_out
