# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Package macro."""

def package(owners = None, **kwargs):  # buildifier: disable=unused-variable
    """A wrapper for the native `package` function to define a plain list of code owners for the package.

    Args:
        owners: a plain list of strings with github usernames.
        **kwargs: passed through to the bazel's native package.
    """
    native.filegroup(name = "Only one 'package' macro is allowed per target. Please check your BUILD.bazel file and remove the second 'package' macro.")
    if kwargs:
        native.package(**kwargs)  # buildifier: disable=native-package
