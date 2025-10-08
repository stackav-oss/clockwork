// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/logging/offboard/channel_message_writer.hh"

#include "jewels/std/expected.hh"

#include <algorithm>
#include <functional>
#include <map>
#include <memory_resource>
#include <utility>
#include <vector>

namespace clockwork_logging::offboard
{

ChannelMessageWriter::ChannelMessageWriter(
  jewels::memory::MemoryResource memory_resource,
  MessageChunkIndexFormat message_chunk_index_format,
  CompressionType compression_type,
  jewels::memory::NonNullSharedPtr<ChunkCompressor> chunk_compressor_ptr,
  jewels::memory::NonNullSharedPtr<ChunkWriter> chunk_writer_ptr,
  jewels::memory::NonNullSharedPtr<AsyncWorkQueue> async_work_queue_ptr)
  : memory_resource_(std::move(memory_resource)),
    message_chunk_index_format_(message_chunk_index_format),
    compression_type_(compression_type),
    chunk_compressor_ptr_(std::move(chunk_compressor_ptr)),
    chunk_writer_ptr_(std::move(chunk_writer_ptr)),
    async_work_queue_ptr_(std::move(async_work_queue_ptr)),
    index_(memory_resource_),
    message_chunk_writer_ptr_(
      std::allocate_shared<MessageChunkWriter, std::pmr::polymorphic_allocator<MessageChunkWriter>>(
        memory_resource_, memory_resource_, message_chunk_index_format_, compression_type_))
{
  index_.reserve(initial_index_capacity);
}

ChannelMessageWriter::~ChannelMessageWriter()
{
  wait_for_pending_async_write_requests();
}

[[nodiscard]] LogExpected<void>
ChannelMessageWriter::add_message(size_t data_size, const ZeroCopyLoggedMessage& message)
{
  if (is_finalized_)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (const auto add_result = message_chunk_writer_ptr_->add_message(data_size, message); !add_result)
  {
    return add_result;
  }
  if (message_chunk_writer_ptr_->is_full())
  {
    if (const auto flush_result = flush_current_chunk(); !flush_result)
    {
      return flush_result;
    }
  }
  return {};
}

[[nodiscard]] LogExpected<std::pmr::vector<IndexChunkIndexEntry>> ChannelMessageWriter::finalize()
{
  if (const auto flush_result = flush_current_chunk(); !flush_result)
  {
    return jewels::unexpected(flush_result.error());
  }
  wait_for_pending_async_write_requests();
  if (const auto async_write_result = get_async_write_result(); !async_write_result)
  {
    return jewels::unexpected(async_write_result.error());
  }
  std::sort(index_.begin(), index_.end());
  return std::move(index_);
}

[[nodiscard]] LogExpected<void> ChannelMessageWriter::flush_current_chunk()
{
  if (!message_chunk_writer_ptr_->is_empty())
  {
    {
      const std::scoped_lock guard{mutex_};
      ++num_pending_async_writes_;
    }
    async_work_queue_ptr_->schedule_work_item(
      [this, message_chunk_writer_ptr = message_chunk_writer_ptr_]()
      {
        const auto write_result = message_chunk_writer_ptr->write_chunk(*chunk_compressor_ptr_, *chunk_writer_ptr_);
        const std::scoped_lock guard{mutex_};
        if (!write_result)
        {
          async_write_result_ = jewels::unexpected(write_result.error());
        }
        else
        {
          add_index_entry(write_result.value());
        }
        --num_pending_async_writes_;
        condvar_.notify_one();
      });
    message_chunk_writer_ptr_ =
      std::allocate_shared<MessageChunkWriter, std::pmr::polymorphic_allocator<MessageChunkWriter>>(
        memory_resource_, memory_resource_, message_chunk_index_format_, compression_type_);
  }
  return get_async_write_result();
}

void ChannelMessageWriter::add_index_entry(const IndexChunkIndexEntry& entry)
{
  if (index_.size() == index_.capacity())
  {
    index_.reserve(index_.size() + index_growth_size);
  }
  index_.push_back(entry);
}

void ChannelMessageWriter::wait_for_pending_async_write_requests()
{
  std::unique_lock guard{mutex_};
  condvar_.wait(guard, [this] { return num_pending_async_writes_ == 0U; });
}

[[nodiscard]] LogExpected<void> ChannelMessageWriter::get_async_write_result()
{
  const std::scoped_lock guard{mutex_};
  return async_write_result_;
}

} // namespace clockwork_logging::offboard
