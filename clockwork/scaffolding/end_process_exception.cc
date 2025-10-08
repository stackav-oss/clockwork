// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/scaffolding/end_process_exception.hh"

namespace clockwork::scaffolding
{

EndProcessException::EndProcessException(int return_code)
  : return_code_(return_code)
{
}

int EndProcessException::return_code() const noexcept
{
  return return_code_;
}

} // namespace clockwork::scaffolding
