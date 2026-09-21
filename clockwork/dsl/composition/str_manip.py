# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Utilities for string manipulation."""


def camel_from_snake(snake_str: str) -> str:
    """Convert the snake case string to camel case."""
    return "".join([x.capitalize() for x in snake_str.lower().split("_")])


def snake_from_camel(camel_str: str) -> str:
    """Convert the camel case string to snake case."""
    with_semis = "".join(["_" + x.lower() if x.isupper() else x for x in camel_str])
    return with_semis.strip("_")


def upper_snake_from_camel(camel_str: str) -> str:
    """Convert the camel case string to upper snake case."""
    with_semis = "".join(["_" + x if x.isupper() else x.upper() for x in camel_str])
    return with_semis.strip("_")
