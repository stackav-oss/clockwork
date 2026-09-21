# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""A repo extension for detecting the name of the root module."""

load("//tools/rules:empty_repository.bzl", "empty_repository")

generated_file = tag_class(
    attrs = {
        "args": attr.string_list(),
        "generator": attr.label(mandatory = True),
        "name": attr.string(mandatory = True),
        "output": attr.string(mandatory = True),
        "source": attr.label(mandatory = True),
    },
)

_build_file_content = """
load("@clockwork//tools/repo:root_repo_py.bzl", "root_repo_py")

{generated_rules}

root_repo_py(
  name = "root_repo_py",
  root_name = "{root_name}",
  imports = ["."],
  visibility = ["//visibility:public"],
)
"""

_generated_file_rule = """
genrule(
  name = "{name}",
  srcs = ["{source}"],
  outs = ["{output}"],
  cmd = "$(location {generator}) $(location {source}) $@ {args}",
  tools = ["{generator}"],
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

    generated_rules = []
    for module in ctx.modules:
        if module.is_root:
            for generated in module.tags.generated_file:
                generated_rules.append(_generated_file_rule.format(
                    args = " ".join(generated.args),
                    generator = str(generated.generator),
                    name = generated.name,
                    output = generated.output,
                    source = str(generated.source),
                ))

    if generated_rules:
        build_file_content = _build_file_content.format(
            generated_rules = "\n".join(generated_rules),
            root_name = root_name,
        )
    else:
        build_file_content = """
load("@clockwork//tools/repo:root_repo_py.bzl", "root_repo_py")

root_repo_py(
  name = "root_repo_py",
  root_name = "{}",
  imports = ["."],
  visibility = ["//visibility:public"],
)
""".format(root_name)
    empty_repository(name = "root_repo", build_file_content = build_file_content)

root_repo = module_extension(
    implementation = _root_repo_impl,
    tag_classes = {"generated_file": generated_file},
)
