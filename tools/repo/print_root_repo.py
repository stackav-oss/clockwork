# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""A script for testing the root repo extension."""

from root_repo_py import ROOT_REPO


def main() -> None:
    """Print out the detected root repo."""
    print(f"Root repo: {ROOT_REPO}")


if __name__ == "__main__":
    main()
