# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Create an empty repository with just a BUILD file."""

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "workspace_and_buildfile")

def _empty_repository(repository_ctx):
    workspace_and_buildfile(repository_ctx)

empty_repository = repository_rule(
    implementation = _empty_repository,
    attrs = {
        # These attrs are copied from https://github.com/bazelbuild/bazel/blob/master/tools/build_defs/repo/http.bzl
        "build_file": attr.label(
            allow_single_file = True,
            doc =
                "The file to use as the BUILD file for this repository." +
                "This attribute is an absolute label (use '@//' for the main " +
                "repo). The file does not need to be named BUILD, but can " +
                "be (something like BUILD.new-repo-name may work well for " +
                "distinguishing it from the repository's actual BUILD files). " +
                "Either build_file or build_file_content can be specified, but " +
                "not both.",
        ),
        "build_file_content": attr.string(
            doc =
                "The content for the BUILD file for this repository. " +
                "Either build_file or build_file_content can be specified, but " +
                "not both.",
        ),
    },
)
