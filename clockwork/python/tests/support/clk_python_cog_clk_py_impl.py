# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Test implementation of a python cog."""

from itertools import chain

from clockwork.python.tests.support.clk_python_cog_clk_py_dial import PythonCogDial
from clockwork.python.tests.support.test_output_message_clk_nb import TestOutputMessage
from jewels.nanobind.nb_sync_time import SyncTime


class PythonCogImpl:
    """Python test cog implementation."""

    @classmethod
    def execute_cog(cls, dial: PythonCogDial) -> None:
        """Execute the test cog.

        Arguments:
            dial: Test cog dial.
        """
        dial.states.state.state_string = dial.states.python_state["python_state"]
        dial.states.python_state["python_state"] = dial.configs.config.config_string
        output_msg1 = TestOutputMessage()
        output_msg1.time_of_validity = SyncTime(nanoseconds=dial.start_time)
        for buffer in chain(dial.inputs.input1.view, dial.inputs.input2.view):
            output_msg1.message_strings.append(buffer.message.message_string)
        dial.outputs.output1.publish(output_msg1)
        output_msg2 = TestOutputMessage()
        output_msg2.time_of_validity = SyncTime(nanoseconds=dial.start_time + 1)
        output_msg2.message_strings.from_iter(
            [buffer.message.message_string for buffer in dial.inputs.input1.new_msgs_view]
            + [buffer.message.message_string for buffer in dial.inputs.input2.new_msgs_view]
        )
        dial.outputs.output2.publish(output_msg2)
