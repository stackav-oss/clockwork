// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include <execinfo.h>

#include <array>
#include <csignal>
#include <cstring>
#include <memory>
#include <sstream> // IWYU pragma: keep
#include <string_view>
#include <unistd.h>

namespace
{
/// The maximum number of entries in the backtrace buffer.
constexpr int backtrace_max = 100;

void print_stack_trace()
{
  std::array<void*, backtrace_max> arr{};

  const int size = backtrace(arr.data(), backtrace_max);
  backtrace_symbols_fd(arr.data(), size, STDERR_FILENO);
}

void signal_handler(int sig)
{
  std::stringstream strstr;

  // strsignal isn't thread safe and sigdescr_np wasn't added until glibc 2.32 so we need to access the signals list
  // directly.
  std::string_view signal_name = "unknown";
  if (sig > 0 && sig < NSIG)
  {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - This access is bounds checked
    signal_name = std::string_view{sys_siglist[sig]};
  }

  strstr << "Caught signal " << sig << " (" << signal_name << ")\n";
  write(STDERR_FILENO, strstr.str().c_str(), strlen(strstr.str().c_str()));

  print_stack_trace();

  (void)signal(sig, SIG_DFL);
  (void)std::raise(sig);
}

/// This function registers `signal_handler` as a `SIGSEGV` handler when the shared library is loaded.
int __attribute__((constructor)) signal_intercept()
{
  static_cast<void>(signal(SIGABRT, signal_handler));
  static_cast<void>(signal(SIGSEGV, signal_handler));

  return 0;
}
} // namespace
