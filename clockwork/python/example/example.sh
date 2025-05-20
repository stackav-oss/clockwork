#!/bin/bash
# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

cd "${BUILD_WORKSPACE_DIRECTORY}" || exit 1
rm -rf /dev/shm/clockwork/
bazel run //clockwork/python/example:example_exe -- clockwork/python/example/platforms.clockwork.python.example.example.example_sys.proc.tachyon
