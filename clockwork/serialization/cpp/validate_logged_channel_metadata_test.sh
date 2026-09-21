#!/bin/bash -e
# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

if [[ $# -ne 3 ]]; then
  echo "Expected usage: '$0 BINARY PREV_METADATA CURR_METADATA'" > /dev/stderr
  exit 1
fi

BINARY="$1"
PREV_METADATA="$2"
CURR_METADATA="$3"

exec "$BINARY" "$PREV_METADATA" "$CURR_METADATA"
