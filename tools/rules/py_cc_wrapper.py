# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Run a C++ binary under a python wrapper to setup the environment for loading python modules."""

import os
import sys
import sysconfig

if __name__ == "__main__":
    """Execute the cc_binary."""
    my_env = os.environ.copy()
    # Use installed_base (compiled into Python) rather than sys.base_prefix or sys.prefix,
    # both of which point to the venv path when bootstrap_impl=script creates a venv.
    # installed_base always refers to the real Python installation with the full stdlib.
    my_env["PYTHONHOME"] = sysconfig.get_config_var("installed_base") or sys.base_prefix
    # Propagate sys.path so the embedded Python can find runfiles modules.
    python_path = os.pathsep.join(p for p in sys.path if p)
    if python_path:
        my_env["PYTHONPATH"] = python_path
    os.execvpe("wrapped_cc_binary", sys.argv, my_env)
