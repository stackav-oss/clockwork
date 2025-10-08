# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Python object test helpers."""


class TestHelper:
    """Python object test helper class."""

    def __init__(
        self,
    ) -> None:
        """Constructor."""

    def echo_string(
        self,
        input_str: str,
    ) -> str:
        """Echo string.

        Arguments:
            input_str: Input string.

        Returns:
            Returns input string.
        """
        return input_str

    def add_numbers(
        self,
        lhs: int,
        rhs: int,
    ) -> int:
        """Add two numbers.

        Arguments:
            lhs: Left hand argument.
            rhs: Right hand argument.

        Returns:
            Returns sum of lhs and rhs.
        """
        return lhs + rhs

    def echo_memoryview(
        self,
        input_view: memoryview,
        output_view: memoryview,
    ) -> None:
        """Echo a memory view.

        Arguments:
            input_view: Input view.
            output_view: Output view.
        """
        for index in range(len(input_view)):
            output_view[index] = input_view[index]
