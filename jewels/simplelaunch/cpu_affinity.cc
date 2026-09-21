// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "jewels/simplelaunch/cpu_affinity.hh"

#include "jewels/log_cerr/log_cerr.hh"

#include <sched.h>

#include <cerrno>
#include <cstdint>
#include <span>
#include <unistd.h>

namespace jewels::simplelaunch
{

[[nodiscard]] bool set_cpu_affinity(std::span<const int32_t> cpus)
{
  if (cpus.empty())
  {
    return true;
  }
  ::cpu_set_t cpu_set{};
  CPU_ZERO(&cpu_set);
  for (const auto cpu : cpus)
  {
    CPU_SET(cpu, &cpu_set);
  }
  if (sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_set) == -1)
  {
    jewels::log_cerr_error("Failed to set sched affinity: errno {}", errno);
    return false;
  }
  return true;
}

} // namespace jewels::simplelaunch
