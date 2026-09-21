#!/usr/bin/env bash
# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
#
# Wrapper used by sh_test rules that exercise --validate-casing.
# Usage: validate_casing_runner.sh <executable> <pdf>
exec "$1" --validate-casing "$2"
