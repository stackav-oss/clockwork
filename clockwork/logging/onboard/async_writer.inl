// IWYU pragma: private, include "clockwork/logging/onboard/async_writer.hh"
#pragma once

#include "clockwork/logging/onboard/async_writer.hh"

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/log_format.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/std/expected.hh"
#include "jewels/time/sync_time.hh"

#include <fmt/base.h>
#include <liburing.h>
#include <liburing/io_uring.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <iterator>
#include <limits>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace clockwork_logging::onboard
{

template <typename Policy>
AsyncWriter<Policy>::AsyncRequestHandle::AsyncRequestHandle(
  AsyncWriteRequestHandleType async_write_request_handle_in,
  jewels::memory::ObjectPtr<AsyncWriter<Policy>> writer_ptr_in,
  CompletionCallbackPtr callback_ptr_in)
  : async_write_request_handle(std::move(async_write_request_handle_in)),
    writer_ptr(writer_ptr_in),
    callback_ptr(callback_ptr_in)
{
}

template <typename Policy>
AsyncWriter<Policy>::AsyncRequestHandle::AsyncRequestHandle(
  jewels::filesystem::FileDescriptor close_fd_in,
  jewels::memory::ObjectPtr<AsyncWriter<Policy>> writer_ptr_in,
  CompletionCallbackPtr callback_ptr_in)
  : close_fd(std::move(close_fd_in)), writer_ptr(writer_ptr_in), callback_ptr(callback_ptr_in)
{
}

template <typename Policy>
AsyncWriter<Policy>::AsyncWriter(
  jewels::memory::MemoryResource init_memory_resource,
  jewels::memory::MemoryResource runtime_memory_resource,
  WriterEnvironment writer_environment)
  : runtime_memory_resource_(std::move(runtime_memory_resource)),
    guarded_state_(jewels::memory::make_pmr_unique<GuardedState>(runtime_memory_resource_)),
    kits_fs_(runtime_memory_resource_),
    outstanding_async_write_queue_storage_(max_async_requests, init_memory_resource),
    outstanding_async_write_queue_(std::in_place, outstanding_async_write_queue_storage_.data(), max_async_requests),
    async_request_pool_(init_memory_resource, max_async_requests),
    writer_environment_(writer_environment)
{
  kits_fs_.set_verbosity(jewels::filesystem::Filesystem::ErrorVerbosity::verbose);
}

template <typename Policy>
AsyncWriter<Policy>::~AsyncWriter()
{
  if (io_ring_initialized_)
  {
    if (const auto drain_result = drain_async_operations(); !drain_result)
    {
      jewels::log_cerr_warn("Failed to drain async operations in destructor");
    }
    if (file_desc_)
    {
      if (const auto close_result = file_desc_.close(); !close_result)
      {
        jewels::log_cerr_error("Failed to close log file in destructor: {}", close_result.error().message());
      }
    }
    io_uring_queue_exit(&ring_);
  }
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
AsyncWriter<Policy>::open_log(std::string_view log_path, std::string_view log_file_prefix)
{
  if (const auto open_result = open_log_paused(log_path, log_file_prefix); !open_result)
  {
    return open_result;
  }
  return resume_logging();
}

template <typename Policy>
[[nodiscard]] LogExpected<void>
AsyncWriter<Policy>::open_log_paused(std::string_view log_path, std::string_view log_file_prefix)
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state != WriterState::closed)
  {
    return jewels::unexpected(LogError::already_open);
  }
  if (!io_ring_initialized_)
  {
    if (const auto init_result = initialize_io_ring(); !init_result)
    {
      return init_result;
    }
  }
  log_path_ = std::pmr::string{log_path, runtime_memory_resource_};
  log_file_prefix_ = std::pmr::string{log_file_prefix, runtime_memory_resource_};
  log_file_sequence_number_ = 0U;
  if (const auto create_result = kits_fs_.create_directory(log_path); !create_result)
  {
    if (create_result.error() != jewels::filesystem::make_error_code(EEXIST))
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string), "Failed to create log directory: {}", create_result.error().message());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(set_writer_error(LogError::failed_to_create_log_directory, std::move(status_string)));
    }
    const auto sequence_result = get_initial_log_file_sequence_number(log_path);
    if (!sequence_result)
    {
      return jewels::unexpected(sequence_result.error());
    }
    log_file_sequence_number_ = *sequence_result;
  }
  guarded_state_->state.store(WriterState::paused, std::memory_order_release);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::pause_logging()
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (state == WriterState::paused)
  {
    return jewels::unexpected(LogError::paused);
  }
  if (const auto close_result = close_current_log_file(); !close_result)
  {
    return close_result;
  }
  guarded_state_->state.store(WriterState::paused, std::memory_order_release);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::resume_logging()
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state != WriterState::paused)
  {
    return jewels::unexpected(LogError::not_paused);
  }
  guarded_state_->state.store(WriterState::logging, std::memory_order_release);
  return open_next_log_file();
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::close_log()
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  if (state != WriterState::paused)
  {
    if (const auto close_result = close_current_log_file(); !close_result)
    {
      return close_result;
    }
  }
  guarded_state_->state.store(WriterState::closed, std::memory_order_release);
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::write_async(AsyncWriteRequestHandleType async_write_handle)
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  const auto io_vector = async_write_handle->get_io_vector();
  const auto write_size = async_write_handle->get_write_size();
  const auto message_data_size = async_write_handle->get_message_data_size();
  if (const auto emplace_result = outstanding_async_write_queue_.emplace_back(async_write_handle); !emplace_result)
  {
    std::pmr::string status_string{"Failed to add async write to list of pending writes", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_queue_async_write, std::move(status_string)));
  }
  auto async_request_result = async_request_pool_.make_shared_object(
    std::move(async_write_handle),
    jewels::memory::make_non_null_from_ref(*this),
    &AsyncWriter<Policy>::write_completion_callback_fn);
  if (!async_request_result)
  {
    std::pmr::string status_string{"Failed to get async request from pool for async write", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string)));
  }
  const auto sqe_result = get_sqe_ptr();
  if (!sqe_result)
  {
    outstanding_async_write_queue_.pop_back();
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (!guarded_state_->maybe_oldest_pending_data_timestamp.load(std::memory_order_relaxed).has_value())
  {
    guarded_state_->maybe_oldest_pending_data_timestamp.store(
      outstanding_async_write_queue_.begin()->value().try_get_oldest_data_timestamp(), std::memory_order_release);
  }
  pending_message_data_bytes_ += message_data_size;
  io_uring_prep_writev(
    *sqe_result, *file_desc_, io_vector.data(), static_cast<uint32_t>(io_vector.size()), log_file_offset_);
  io_uring_sqe_set_data(*sqe_result, async_request_result->release());
  io_uring_sqe_set_flags(*sqe_result, IOSQE_IO_DRAIN);
  log_file_offset_ += write_size;
  ++pending_request_count_;
  return {};
}

template <typename Policy>
void AsyncWriter<Policy>::periodic_callback()
{
  if (io_ring_initialized_)
  {
    submit_queued_async_operations();
    process_completed_async_operations();
  }
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::drain_async_operations()
{
  if (!io_ring_initialized_)
  {
    return {};
  }
  submit_queued_async_operations();
  process_completed_async_operations();
  while (pending_request_count_ != 0U)
  {
    std::this_thread::sleep_for(drain_sleep_time);
    process_completed_async_operations();
  }
  if (guarded_state_->state.load(std::memory_order_relaxed) == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<size_t> AsyncWriter<Policy>::get_pending_request_count() const
{
  if (guarded_state_->state.load(std::memory_order_relaxed) == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  return pending_request_count_;
}

template <typename Policy>
[[nodiscard]] size_t AsyncWriter<Policy>::get_pending_message_data_bytes() const
{
  return pending_message_data_bytes_;
}

template <typename Policy>
[[nodiscard]] LogExpected<std::chrono::nanoseconds>
AsyncWriter<Policy>::get_write_backlog(jewels::time::SteadyTime current_steady_time) const
{
  const auto state = guarded_state_->state.load(std::memory_order_acquire);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_acquire).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  return current_steady_time - guarded_state_->maybe_oldest_pending_data_timestamp.load(std::memory_order_acquire)
                                 .value_or(current_steady_time);
}

template <typename Policy>
[[nodiscard]] WriterState AsyncWriter<Policy>::get_state() const
{
  return guarded_state_->state.load(std::memory_order_acquire);
}

template <typename Policy>
[[nodiscard]] std::pmr::string AsyncWriter<Policy>::get_status_string() const
{
  const std::lock_guard guard(guarded_state_->mutex);
  return guarded_state_->status_string;
}

template <typename Policy>
[[nodiscard]] LogExpected<size_t> AsyncWriter<Policy>::get_log_file_offset() const
{
  const auto state = guarded_state_->state.load(std::memory_order_relaxed);
  if (state == WriterState::failed)
  {
    return jewels::unexpected{
      guarded_state_->maybe_writer_error.load(std::memory_order_relaxed).value_or(LogError::failed)};
  }
  if (state == WriterState::closed)
  {
    return jewels::unexpected(LogError::not_open);
  }
  return log_file_offset_;
}

template <typename Policy>
[[nodiscard]] size_t AsyncWriter<Policy>::get_avail_async_request_handles() const
{
  return async_request_pool_.get_avail_count();
}

template <typename Policy>
[[nodiscard]] size_t AsyncWriter<Policy>::get_and_reset_drop_count()
{
  return guarded_state_->drop_count.exchange(0U, std::memory_order_release);
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::initialize_io_ring()
{
  if (const auto ret = io_uring_queue_init(io_ring_size, &ring_, 0U); ret < 0)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string),
      "io_uring_queue_init failed: {}",
      jewels::filesystem::make_error_code(-ret).message());
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_initialize_io_ring, std::move(status_string)));
    ;
  }
  io_ring_initialized_ = true;
  if (const auto ret = io_uring_ring_dontfork(&ring_); ret < 0)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string),
      "io_uring_ring_dontfork failed: {}",
      jewels::filesystem::make_error_code(-ret).message());
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_initialize_io_ring, std::move(status_string)));
  }
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<uint32_t> AsyncWriter<Policy>::get_initial_log_file_sequence_number(std::string_view log_path)
{
  const auto readdir_result = kits_fs_.read_directory(
    log_path,
    [this](const auto& dent)
    {
      const std::string_view name{&dent.d_name[0U]};
      return (dent.d_type == DT_REG) && name.starts_with(log_file_prefix_) && name.ends_with(log_file_suffix);
    });
  if (!readdir_result)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Failed to read existing log directory: {}", readdir_result.error().message());
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(
      set_writer_error(LogError::failed_to_get_initial_log_file_sequence_number, std::move(status_string)));
  }
  std::optional<uint32_t> maybe_max_sequence_number;
  for (const auto& file_name : *readdir_result)
  {
    const auto sequence_str =
      std::pmr::string{file_name.string_view().substr(log_file_prefix_.size()), runtime_memory_resource_};
    char* end_ptr = nullptr; // NOLINT(misc-const-correctness) This is a false positive. This pointer cannot be const.
    constexpr auto base_10 = 10;
    const auto sequence_number = strtoul(sequence_str.c_str(), &end_ptr, base_10);
    if (
      (sequence_number == ULONG_MAX && errno == ERANGE) || (*end_ptr != log_file_suffix.front()) ||
      sequence_number > std::numeric_limits<uint32_t>::max())
    {
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(
        std::back_inserter(status_string),
        "Found invalid file name '{}' in existing log directory",
        file_name.string_view());
      jewels::log_cerr_error("{}", status_string);
      return jewels::unexpected(
        set_writer_error(LogError::failed_to_get_initial_log_file_sequence_number, std::move(status_string)));
    }
    maybe_max_sequence_number = std::max(
      maybe_max_sequence_number.value_or(static_cast<uint32_t>(sequence_number)),
      static_cast<uint32_t>(sequence_number));
  }
  if (!maybe_max_sequence_number)
  {
    return 0U;
  }
  return *maybe_max_sequence_number + 1U;
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::open_next_log_file()
{
  std::pmr::string file_path{runtime_memory_resource_};
  fmt::format_to(
    std::back_inserter(file_path),
    fmt::runtime("{}/{}{:06d}{}"),
    log_path_,
    log_file_prefix_,
    log_file_sequence_number_,
    log_file_suffix);
  ++log_file_sequence_number_;
  auto open_result = kits_fs_.open(
    file_path,
    O_CREAT | O_EXCL | (writer_environment_ == onboard::WriterEnvironment::simulation ? 0 : O_DIRECT | O_DSYNC) |
      O_WRONLY);
  if (!open_result)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(
      std::back_inserter(status_string), "Failed to open log file '{}': {}", file_path, open_result.error().message());
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_open_log_file, std::move(status_string)));
  }
  file_desc_ = std::move(open_result).value();
  return {};
}

template <typename Policy>
[[nodiscard]] LogExpected<void> AsyncWriter<Policy>::close_current_log_file()
{
  auto async_request_result = async_request_pool_.make_shared_object(
    std::move(file_desc_), jewels::memory::make_non_null_from_ref(*this), &close_completion_callback_fn);
  if (!async_request_result)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Failed to get async request from pool for async close");
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::failed_to_allocate_async_request, std::move(status_string)));
  }
  const auto sqe_result = get_sqe_ptr();
  if (!sqe_result)
  {
    return jewels::unexpected(sqe_result.error());
  }
  io_uring_prep_nop(*sqe_result);
  io_uring_sqe_set_data(*sqe_result, async_request_result->release());
  io_uring_sqe_set_flags(*sqe_result, IOSQE_IO_DRAIN);
  ++pending_request_count_;
  log_file_offset_ = 0U;
  return {};
}

template <typename Policy>
void AsyncWriter<Policy>::submit_queued_async_operations()
{
  if (const auto ret = io_uring_submit(&ring_); ret < 0)
  {
    // If a submit fails there is no way to tell which commands were queued
    // TODO(OI-3032): Replace with better observability/contract mechanism when one is available
    jewels::log_cerr_error("io_uring_submit failed: {}", jewels::filesystem::make_error_code(-ret).message());
    std::terminate();
  }
}

template <typename Policy>
LogExpected<jewels::memory::ObjectPtr<io_uring_sqe>> AsyncWriter<Policy>::get_sqe_ptr()
{
  auto* sqe_ptr = io_uring_get_sqe(&ring_);
  if (sqe_ptr == nullptr)
  {
    submit_queued_async_operations();
    sqe_ptr = io_uring_get_sqe(&ring_);
  }
  if (sqe_ptr == nullptr)
  {
    std::pmr::string status_string{"io_uring_get_sqe failed", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    return jewels::unexpected(set_writer_error(LogError::io_uring_get_sqe_failed, std::move(status_string)));
  }
  return jewels::memory::make_non_null_from_ref(*sqe_ptr);
}

template <typename Policy>
void AsyncWriter<Policy>::process_completed_async_operations()
{
  if (pending_request_count_ == 0U)
  {
    return;
  }
  uint32_t request_count = 0U;
  uint32_t head = 0U;
  const io_uring_cqe* cqe_ptr = nullptr;
  io_uring_for_each_cqe(&ring_, head, cqe_ptr)
  {
    typename jewels::SharedObjectPool<AsyncRequestHandle>::SharedReference request_reference{
      static_cast<typename jewels::SharedObjectPool<AsyncRequestHandle>::PoolEntryType*>(
        io_uring_cqe_get_data(cqe_ptr))};
    request_reference->callback_ptr(request_reference->writer_ptr, cqe_ptr->res, request_reference.value());
    ++request_count;
    --pending_request_count_;
  }
  io_uring_cq_advance(&ring_, request_count);
}

template <typename Policy>
void AsyncWriter<Policy>::write_completion_callback_fn(
  jewels::memory::ObjectPtr<AsyncWriter> writer_ptr, int32_t result, AsyncRequestHandle& request_handle)
{
  writer_ptr->write_completion_callback(result, request_handle);
}

template <typename Policy>
void AsyncWriter<Policy>::write_completion_callback(int32_t result, AsyncRequestHandle& request_handle)
{
  pending_message_data_bytes_ -= request_handle.async_write_request_handle->get_message_data_size();
  if (
    outstanding_async_write_queue_.empty() ||
    *outstanding_async_write_queue_.begin() != request_handle.async_write_request_handle)
  {
    if (!outstanding_async_write_queue_.empty())
    {
      outstanding_async_write_queue_.pop_front();
    }
    if (!outstanding_async_write_queue_.empty())
    {
      guarded_state_->maybe_oldest_pending_data_timestamp.store(
        outstanding_async_write_queue_.begin()->value().try_get_oldest_data_timestamp(), std::memory_order_release);
    }
    else
    {
      guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
    }
    std::pmr::string status_string{"Async writes are completing out of order", runtime_memory_resource_};
    jewels::log_cerr_error("{}", status_string);
    set_writer_error_no_return(LogError::async_writes_completed_out_of_order, std::move(status_string));
    return;
  }
  outstanding_async_write_queue_.pop_front();
  if (!outstanding_async_write_queue_.empty())
  {
    guarded_state_->maybe_oldest_pending_data_timestamp.store(
      outstanding_async_write_queue_.begin()->value().try_get_oldest_data_timestamp(), std::memory_order_release);
  }
  else
  {
    guarded_state_->maybe_oldest_pending_data_timestamp.store(std::nullopt, std::memory_order_release);
  }
  if (result < 0)
  {
    if (-result == EIO)
    {
      if (guarded_state_->state.load(std::memory_order_relaxed) != WriterState::failed)
      {
        std::pmr::string status_string{"Async write failed: I/O error", runtime_memory_resource_};
        jewels::log_cerr_error("{}", status_string);
        const std::lock_guard guard(guarded_state_->mutex);
        guarded_state_->status_string = std::move(status_string);
        guarded_state_->maybe_writer_error.store(LogError::io_error, std::memory_order_release);
        guarded_state_->state.store(WriterState::degraded, std::memory_order_release);
      }
    }
    else
    {
      const auto error_code = jewels::filesystem::make_error_code(-result);
      std::pmr::string status_string{runtime_memory_resource_};
      fmt::format_to(std::back_inserter(status_string), "async write failed: {}", error_code.message());
      jewels::log_cerr_error("{}", status_string);
      set_writer_error_no_return(to_log_error(error_code), std::move(status_string));
    }
    if (
      guarded_state_->drop_count.fetch_add(
        request_handle.async_write_request_handle->message_handles().size(), std::memory_order_release) == 0U)
    {
      jewels::log_cerr_warn(
        "Dropped {} messages due to I/O error", request_handle.async_write_request_handle->message_handles().size());
    }
    return;
  }
  for (const auto& message_handle : request_handle.async_write_request_handle->message_handles())
  {
    if (!message_handle.is_valid())
    {
      if (guarded_state_->drop_count.fetch_add(1U, std::memory_order_release) == 0U)
      {
        jewels::log_cerr_warn("Detected overrun in write completion callback");
      }
    }
  }
}

template <typename Policy>
void AsyncWriter<Policy>::close_completion_callback_fn(
  jewels::memory::ObjectPtr<AsyncWriter> writer_ptr, int32_t /*result*/, AsyncRequestHandle& request_handle)
{
  writer_ptr->close_completion_callback(request_handle);
}

template <typename Policy>
void AsyncWriter<Policy>::close_completion_callback(AsyncRequestHandle& request_handle)
{
  if (const auto close_result = request_handle.close_fd.close(); !close_result)
  {
    std::pmr::string status_string{runtime_memory_resource_};
    fmt::format_to(std::back_inserter(status_string), "Failed to close log file: {}", close_result.error().message());
    jewels::log_cerr_error("{}", status_string);
    set_writer_error_no_return(LogError::failed_to_close_log_file, std::move(status_string));
  }
}

template <typename Policy>
void AsyncWriter<Policy>::set_writer_error_no_return(LogError writer_error, std::string_view status_string)
{
  const std::lock_guard guard(guarded_state_->mutex);
  if (guarded_state_->status_string.empty())
  {
    guarded_state_->status_string = std::pmr::string(status_string, runtime_memory_resource_);
  }
  guarded_state_->maybe_writer_error.store(writer_error, std::memory_order_release);
  guarded_state_->state.store(WriterState::failed, std::memory_order_release);
}

template <typename Policy>
[[nodiscard]] LogError AsyncWriter<Policy>::set_writer_error(LogError writer_error, std::string_view status_string)
{
  set_writer_error_no_return(writer_error, status_string);
  return writer_error;
}

} // namespace clockwork_logging::onboard
