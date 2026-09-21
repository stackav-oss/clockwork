# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Package macro."""

def package(owners = None, file_owners = None, **kwargs):  # buildifier: disable=unused-variable
    """A wrapper for the native `package` function to define a plain list of code owners for the package.

    Args:
        owners: a plain list of strings with github usernames, e.g. "@user".
        file_owners: a dictionary mapping file globs to lists of owners, e.g. `{"*.txt": ["@user"]}`. Similar to
            the above argument but for file globs instead of for the whole directory.
        **kwargs: passed through to the bazel's native package.
    """
    native.filegroup(name = "Only one 'package' macro is allowed per target. Please check your BUILD.bazel file and remove the second 'package' macro.", tags = ["no-lint"])
    if kwargs:
        native.package(**kwargs)  # buildifier: disable=native-package
