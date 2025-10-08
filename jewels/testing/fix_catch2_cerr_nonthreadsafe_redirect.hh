// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __has_feature
#if __has_feature(thread_sanitizer)
__attribute__((constructor)) void fix_catch2_cerr_nonthreadsafe_redirect()
{
  // catch2 unilaterally redirects cerr to a plain (non-threadsafe) stringstream if it detects that it's running under
  // bazel with a known output file (which it will then report to).  This is normally tolerable, but it breaks tsan so I
  // need to disable it.  Catch2 somehow doesn't offer a way to avoid this, but if the output file envvar is unset then
  // it won't attempt the redirection.
  ::unsetenv("XML_OUTPUT_FILE");
}
#endif
#endif
