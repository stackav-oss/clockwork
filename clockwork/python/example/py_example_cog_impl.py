# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Test implementation of a python cog."""

from clockwork.python.example.py_example_cog_dial import (
    ExampleInitDial,
    ExampleInput1Dial,
    ExampleInput2Dial,
    ExampleOutputDial,
)
from clockwork.python.tests.support.py_test_input_message import TestInputMessage
from clockwork.python.tests.support.test_output_message_clk_nb import TestOutputMessage
from jewels.nanobind.nb_sync_time import SyncTime


class ExampleInitImpl:
    """Example cog implementation."""

    @classmethod
    def execute_cog(cls, dial: ExampleInitDial) -> None:
        """Initialize the example cogs.

        Arguments:
            dial: Init cog dial.
        """
        dial.states.input1_state.state_string = dial.configs.input1_config.config_string
        dial.states.input2_state.state_string = dial.configs.input2_config.config_string
        dial.states.python_state["input1_counter"] = 1
        dial.states.python_state["input2_counter"] = 1


class ExampleInput1Impl:
    """Input1 cog implementation."""

    @classmethod
    def execute_cog(cls, dial: ExampleInput1Dial) -> None:
        """Publish on the input1 channel.

        Arguments:
            dial: Input1 cog dial.
        """
        counter = dial.states.python_state["input1_counter"]
        output_message = TestInputMessage()
        output_message.message_string = f"{dial.states.state.state_string}: {counter}"
        dial.outputs.output.publish(output_message)
        dial.states.python_state["input1_counter"] = counter + 1


class ExampleInput2Impl:
    """Input2 cog implementation."""

    @classmethod
    def execute_cog(cls, dial: ExampleInput2Dial) -> None:
        """Publish on the input2 channel.

        Arguments:
            dial: Input2 cog dial.
        """
        counter = dial.states.python_state["input2_counter"]
        output_message = TestInputMessage()
        output_message.message_string = f"{dial.states.state.state_string}: {counter}"
        dial.outputs.output.publish(output_message)
        dial.states.python_state["input2_counter"] = counter + 1


class ExampleOutputImpl:
    """Output cog implementation."""

    @classmethod
    def execute_cog(cls, dial: ExampleOutputDial) -> None:
        """Echo messages from the input channels to the output channels.

        Arguments:
            dial: Input2 cog dial.
        """
        output_message = TestOutputMessage()
        output_message.time_of_validity = SyncTime(nanoseconds=dial.start_time)
        output_message.message_strings = [
            buffer.message.message_string for buffer in dial.inputs.input1.new_msgs_view
        ] + [buffer.message.message_string for buffer in dial.inputs.input2.new_msgs_view]
        dial.outputs.output.publish(output_message)
