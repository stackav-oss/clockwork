# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Macro to generate a py file with the name of the root repo.

This is not meant to be used outside of the root_repo extension."""

load("//tools/rules:python.bzl", "py_library")

def root_repo_py(name, root_name, **kwargs):
    out_file = name + ".py"
    native.genrule(
        name = name + "_pygen",
        srcs = [],
        outs = [out_file],
        cmd = "echo 'from typing import Final\nROOT_REPO: Final = \"{}\"' > $@".format(root_name),
    )

    py_library(
        name = name,
        srcs = [out_file],
        **kwargs
    )
