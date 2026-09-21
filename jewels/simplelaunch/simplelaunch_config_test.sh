#!/bin/bash -e
# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

if [[ $# -ne 2 ]]; then
  echo "Expected usage: '$0 BINARY CONFIG'" > /dev/stderr
  exit 1
fi

BINARY="$1"
CONFIG="$2"

exec "$BINARY" validate "$CONFIG"
