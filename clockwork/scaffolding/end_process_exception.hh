// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <exception>

namespace clockwork::scaffolding
{

///
/// Throwing this exception will cause the clockwork process to exit normally with the provided return code.
/// Derives from std::exception to satisfy hicpp-exception-baseclass
///
class EndProcessException : std::exception
{
public:
  explicit EndProcessException(int return_code);

  [[nodiscard]] int return_code() const noexcept;

private:
  int return_code_;
};

} // namespace clockwork::scaffolding
