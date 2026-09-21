// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

// IWYU pragma: private, include "clockwork/logging/onboard/tests/support/test_support.hh"
#pragma once

#include "clockwork/logging/onboard/tests/support/test_support.hh"

#include "clockwork/logging/onboard/types.hh"
#include "jewels/memory/memory_resource.hh"

#include <algorithm>
#include <fstream> // IWYU pragma: keep
#include <memory>
#include <memory_resource>
#include <utility>

namespace clockwork_logging::onboard::tests
{

template <OnboardBufferedReaderType BufferedReaderType>
[[nodiscard]] std::shared_ptr<BufferedReaderType> make_buffered_reader()
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  return std::make_shared<BufferedReaderType>(memory_resource);
}

template <OffboardBufferedReaderType BufferedReaderType>
[[nodiscard]] std::shared_ptr<BufferedReaderType> make_buffered_reader()
{
  const jewels::memory::MemoryResource memory_resource{std::pmr::new_delete_resource()};
  auto chunk_reader_factory = std::make_shared<typename BufferedReaderType::ChunkReaderFactoryType>(memory_resource);
  return std::make_shared<BufferedReaderType>(memory_resource, std::move(chunk_reader_factory));
}

} // namespace clockwork_logging::onboard::tests
