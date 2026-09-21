// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace clockwork::pinion
{

/// Attempts to set or clear the O_NONBLOCKING flag on the given socket
/// @param socket_fd the file descriptor of the socket to change
/// @param value true if the socket should be nonblocking, false if it should be blocking
/// @return true if successful, false if an error occurred
[[nodiscard]] bool set_nonblocking(int socket_fd, bool value) noexcept;

} // namespace clockwork::pinion
