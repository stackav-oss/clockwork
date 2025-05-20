# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""A repo extension for detecting the name of the root module."""

load("//tools/rules:empty_repository.bzl", "empty_repository")

_build_file_content = """
load("@clockwork//tools/repo:root_repo_py.bzl", "root_repo_py")

root_repo_py(
  name = "root_repo_py",
  root_name = "{}",
  imports = ["."],
  visibility = ["//visibility:public"],
)
"""

_error_message = """
Clockwork is unable to detect root module.  Please add the following to your MODULE.bazel file:

root_repo = use_extension("@clockwork//tools/repo:root_repo.bzl", "root_repo")
use_repo(root_repo, "root_repo")
"""

def _root_repo_impl(ctx):
    root_name = None
    for m in ctx.modules:
        if m.is_root:
            root_name = m.name
            break

    if not root_name:
        fail(_error_message)

    empty_repository(
        name = "root_repo",
        build_file_content = _build_file_content.format(root_name),
    )

root_repo = module_extension(
    implementation = _root_repo_impl,
)
