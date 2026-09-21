// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/testing/fix_catch2_cerr_nonthreadsafe_redirect.hh"

#include <cstdlib>

// catch2 unilaterally redirects cerr to a plain (non-threadsafe) stringstream if it detects that it's running under
// bazel with a known output file (which it will then report to).  This is normally tolerable, but it breaks
// multi-threaded tests so we need to disable it.  Catch2 somehow doesn't offer a way to avoid this, but if the output
// file envvar is unset then it won't attempt the redirection.
__attribute__((constructor)) void fix_catch2_cerr_nonthreadsafe_redirect()
{
  // NOLINTNEXTLINE(concurrency-mt-unsafe) Required for multi-threaded unit tests, runs at startup
  ::unsetenv("XML_OUTPUT_FILE");
}
