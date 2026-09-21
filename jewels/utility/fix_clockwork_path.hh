// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <string_view>

namespace jewels
{

/// Try to fix a path to support tests run from outside of the clockwork repo.
///
/// When tests are run from an external repo the path needs to be prefixed
/// with "../clockwork+/". If the file doesn't exist then try prefixing with
/// "../clockwork+/" before failing to locate the file.
///
/// Returns the original string if no file exists with either the original
/// path or the fixed (prefixed) path.
///
/// @param[in] file_path Clockwork file path
/// @return File path to use to access the file
[[nodiscard]] std::string fix_clockwork_path(std::string_view file_path);

} // namespace jewels
