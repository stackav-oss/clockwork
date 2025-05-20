// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_init.hh"

#include <Python.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace clockwork::python
{

void python_init_once()
{
  // Python expects the thread that calls Py_Initialize to run for the
  // life of the interpreter.
  [[maybe_unused]] static const auto py_initialized = []()
  {
    static std::atomic<bool> initialized_flag{false};
    auto init_thread = std::thread{[]()
                                   {
                                     Py_InitializeEx(0);
                                     const auto* thread_state = PyEval_SaveThread();
                                     initialized_flag = true;
                                     while (true)
                                     {
                                       std::this_thread::sleep_for(std::chrono::hours(1));
                                     }
                                     // We can't restore the thread from the exit handler.
                                     (void)thread_state;
                                   }};
    // Detach the thread so we don't get terminated in the exit handler.
    init_thread.detach();
    // Wait for python to initialize, it won't be long.
    while (!initialized_flag)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
  }();
}

} // namespace clockwork::python
