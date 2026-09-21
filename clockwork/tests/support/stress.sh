#!/bin/bash
# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

export VEHICLE_ID=unknown
rm -rf /dev/shm/clockwork/
../clockwork+/clockwork/tests/support/stress_exe ../clockwork+/clockwork/tests/support/clockwork.clockwork.tests.support.stress.stress_sys.stress_proc.tachyon
