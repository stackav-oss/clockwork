# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Run a C++ binary under a python wrapper to setup the environment for loading python modules."""
# TODO(OI-3125): De-duplicate wrapper.

import os
import sys

if __name__ == "__main__":
    """Execute the cc_binary."""
    my_env = os.environ.copy()
    my_env["PYTHONHOME"] = sys.prefix
    os.execvpe("wrapped_cc_binary", sys.argv, my_env)
