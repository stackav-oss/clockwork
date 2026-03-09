// IWYU pragma: private, include "clockwork/pinion/sock_opt.hh"
#pragma once

#include "clockwork/pinion/sock_opt.hh"

#include "jewels/filesystem/error_code.hh"
#include "jewels/networking/sock_opt.hh"
#include "jewels/std/expected.hh"

#include <tuple>
#include <variant>

namespace clockwork::pinion
{

template <jewels::networking::SockOption... options>
  requires(SupportedSockOption<options> && ...)
jewels::expected<void, jewels::filesystem::ErrorCode>
handle_sock_options(int file_desc, const std::tuple<SockOptionValue<options>...>& values)
{
  jewels::expected<void, jewels::filesystem::ErrorCode> result{};
  std::apply(
    [&result, file_desc](const auto&... value_pack)
    {
      auto handler = [&result, file_desc](const auto& value)
      {
        if (!result)
        {
          return;
        }
        result = handle_sock_option(file_desc, value);
      };
      (handler(value_pack), ...);
    },
    values);
  return result;
}

} // namespace clockwork::pinion
