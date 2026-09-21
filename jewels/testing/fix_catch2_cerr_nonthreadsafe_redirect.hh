// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

__attribute__((constructor)) void fix_catch2_cerr_nonthreadsafe_redirect();
