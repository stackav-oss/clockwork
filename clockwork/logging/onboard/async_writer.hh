// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "clockwork/logging/log_error.hh"
#include "clockwork/logging/onboard/types.hh"
#include "clockwork/logging/onboard/writer_state.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/filesystem/file_descriptor.hh"
#include "jewels/filesystem/filesystem.hh"
#include "jewels/memory/aligned_storage.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pmr_unique_ptr.hh"
#include "jewels/memory/pointers.hh"
#include "jewels/shared_pool/shared_object_pool.hh"
#include "jewels/time/sync_time.hh"

#include <liburing.h>
#include <liburing/io_uring.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clockwork_logging::onboard
{

/// Asynchronous low level log writer
///
/// This class is designed to run in a multithreaded environment where one thread
/// is writing messages to the log and another thread is reporting the status.
/// Unless otherwise indicated, methods on this class *SHALL ONLY* be called
/// from the thread doing the writing.
///
/// Errors from write and close operations are reported asynchronously.  After an
/// unrecoverable error occurs the writer becomes unusable. The writer state is set
/// to failed and future operations fail with LogError::failed.
///
/// After a recoverable error occurs (typically EIO) the writer state is set to degraded
/// and the writer remains usable. This interface does not provide for retrying failed
/// writes and there are no guarantees that the data was or was not written to the log
/// file.
///
/// @tparam Policy Asynchronous writer policy
template <typename Policy>
class AsyncWriter
{
  /// Struct used to store state shared between the writer thread and the thread
  /// that reports the writer status
  struct GuardedState
  {
    /// Oldest timestamp of data in outstanding async writes
    std::atomic<std::optional<jewels::time::SteadyTime>> maybe_oldest_pending_data_timestamp;

    /// Writer error code
    std::atomic<std::optional<LogError>> maybe_writer_error;

    /// Number of dropped messages
    std::atomic<size_t> drop_count{0U};

    /// Human readable status string set when the state is failed or degraded
    std::pmr::string status_string;

    /// Mutex guarding the status string
    mutable std::mutex mutex;

    /// Writer state
    std::atomic<WriterState> state{WriterState::closed};
  };

public:
  /// I/O ring size
  static constexpr uint32_t io_ring_size = Policy::io_ring_size;

  /// Maximum number of outstanding asynchronous write operations
  static constexpr size_t max_async_requests = Policy::max_async_requests;

  /// Time to sleep waiting for async operations to drain
  static constexpr auto drain_sleep_time = std::chrono::milliseconds(10);

  /// Async write request handle type
  using AsyncWriteRequestHandleType = typename Policy::AsyncWriteRequestHandleType;

  /// Message handle type
  using MessageHandleType = typename Policy::MessageHandleType;

  /// Buffer reference type
  using BufferReferenceType = typename Policy::BufferReferenceType;

  /// Async request handle
  struct AsyncRequestHandle
  {
    /// Async write request handle type
    using AsyncWriteRequestHandleType = typename Policy::AsyncWriteRequestHandleType;

    /// Completion callback pointer type
    using CompletionCallbackPtr =
      void (*)(jewels::memory::ObjectPtr<AsyncWriter<Policy>>, int32_t, AsyncRequestHandle&);

    /// Construct an async request handle for a write operation
    /// @param[in] async_write_request_handle_in Async write request handle
    /// @param[in] writer_ptr_in Async writer pointer
    /// @param[in] callback_ptr_in Completion callback pointer
    AsyncRequestHandle(
      AsyncWriteRequestHandleType async_write_request_handle_in,
      jewels::memory::ObjectPtr<AsyncWriter<Policy>> writer_ptr_in,
      CompletionCallbackPtr callback_ptr_in);

    /// Construct an async request handle for a close operation
    /// @param[in] close_fd_in Close file descriptor
    /// @param[in] writer_ptr_in Async writer pointer
    /// @param[in] callback_ptr_in Completion callback pointer
    AsyncRequestHandle(
      jewels::filesystem::FileDescriptor close_fd_in,
      jewels::memory::ObjectPtr<AsyncWriter<Policy>> writer_ptr_in,
      CompletionCallbackPtr callback_ptr_in);

    ~AsyncRequestHandle() = default;

    AsyncRequestHandle(const AsyncRequestHandle&) = delete;
    AsyncRequestHandle& operator=(const AsyncRequestHandle&) = delete;
    AsyncRequestHandle(AsyncRequestHandle&&) = delete;
    AsyncRequestHandle& operator=(AsyncRequestHandle&&) = delete;

    /// Async write request handle for write requests
    AsyncWriteRequestHandleType async_write_request_handle{};

    /// File descriptor for close requests
    jewels::filesystem::FileDescriptor close_fd;

    /// Async writer pointer
    jewels::memory::ObjectPtr<AsyncWriter<Policy>> writer_ptr;

    /// Completion callback pointer
    CompletionCallbackPtr callback_ptr;
  };

  /// Constructor
  /// @param[in] init_memory_resource Memory resource used to allocate memory during initialization
  /// @param[in] runtime_memory_resource Memory resource used to allocate memory after initialization
  /// @param[in] writer_environment Writer environment type
  AsyncWriter(
    jewels::memory::MemoryResource init_memory_resource,
    jewels::memory::MemoryResource runtime_memory_resource,
    WriterEnvironment writer_environment);

  /// Destructor waits for all outstanding async requests to complete.
  /// Data maybe be lost if an error occurs while draining the outstanding
  /// async requests. The caller should call drain_async_operations to get
  /// notified if any outstanding I/O operations fail.
  ~AsyncWriter();

  AsyncWriter(const AsyncWriter&) = delete;
  AsyncWriter& operator=(const AsyncWriter&) = delete;
  AsyncWriter(AsyncWriter&&) = delete;
  AsyncWriter& operator=(AsyncWriter&&) = delete;

  /// Open the writer
  /// @param[in] log_path Path to the directory that contains the log
  /// @param[in] log_file_prefix Log file name prefix
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_log(std::string_view log_path, std::string_view log_file_prefix);

  /// Open the writer in a paused state, logging doesn't start until resume logging is called
  /// @param[in] log_path Path to the directory that contains the log
  /// @param[in] log_file_prefix Log file name prefix
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_log_paused(std::string_view log_path, std::string_view log_file_prefix);

  /// Pause logging, closes the current log file
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> pause_logging();

  /// Resume logging in the next log file
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> resume_logging();

  /// Close the current file and stop writing the the current directory
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> close_log();

  /// Schedule an asynchronous write
  /// @param[in] write_request Asynchronous write request
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> write_async(AsyncWriteRequestHandleType async_write_handle);

  /// Periodic callback to manage async operations
  void periodic_callback();

  /// Wait for all outstanding async operations to complete
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> drain_async_operations();

  /// Get the number of pending async operations
  /// @return Pending request count or log error on failure
  [[nodiscard]] LogExpected<size_t> get_pending_request_count() const;

  /// Get the current write backlog
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @param[in] Current steady time
  /// @return Age of the oldest pending write data or log error on failure
  [[nodiscard]] LogExpected<std::chrono::nanoseconds>
  get_write_backlog(jewels::time::SteadyTime current_steady_time) const;

  /// Get the current writer state
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Writer state
  [[nodiscard]] WriterState get_state() const;

  /// Get the human readable status string set when the state is failed or degraded
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @return Status string
  [[nodiscard]] std::pmr::string get_status_string() const;

  /// Get the current log file offset
  /// @return Log file offset, or zero if the writer has failed
  [[nodiscard]] LogExpected<size_t> get_log_file_offset() const;

  /// Get the number of available async request handles
  [[nodiscard]] size_t get_avail_async_request_handles() const;

  /// Get and reset the number of messages dropped due to the write backlog being exceeded
  /// @note This method *MAY* be called by the thread that reports the writer state
  /// @post The drop count is reset to zero
  /// @return Drop count
  [[nodiscard]] size_t get_and_reset_drop_count();

private:
  /// Initialize the I/O ring
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> initialize_io_ring();

  /// Get the initial log file sequence number when reopening an existing log
  /// @param[in] log_path Log path
  /// @return Log file sequence number of LogError on failure
  [[nodiscard]] LogExpected<uint32_t> get_initial_log_file_sequence_number(std::string_view log_path);

  /// Open the next log file
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> open_next_log_file();

  /// Close the current log file
  /// @return LogError on failure
  [[nodiscard]] LogExpected<void> close_current_log_file();

  /// Submit any queued async operations
  void submit_queued_async_operations();

  /// Get a request pointer from the I/O ring
  /// @return I/O request pointer or log error on failuer
  [[nodiscard]] LogExpected<jewels::memory::ObjectPtr<io_uring_sqe>> get_sqe_ptr();

  /// Process any completed async operations
  void process_completed_async_operations();

  /// Write completion callback function
  /// @param[in] writer_ptr Async writer pointer
  /// @param[in] request_handle Async request handle
  /// @param[in] result Async operation result
  static void write_completion_callback_fn(
    jewels::memory::ObjectPtr<AsyncWriter> writer_ptr, int32_t result, AsyncRequestHandle& request_handle);

  /// Write completion callback
  /// @param[in] result Async operation result
  /// @param[in] request_handle Async request handle
  void write_completion_callback(int32_t result, AsyncRequestHandle& request_handle);

  /// Close completion callback function
  /// @param[in] writer_ptr Async writer pointer
  /// @param[in] result Request result
  /// @param[in] request_handle Async request handle
  static void close_completion_callback_fn(
    jewels::memory::ObjectPtr<AsyncWriter> writer_ptr, int32_t /*result*/, AsyncRequestHandle& request_handle);

  /// Close completion callback
  /// @param[in] request_handle Async request handle
  void close_completion_callback(AsyncRequestHandle& request_handle);

  /// Set the writer error and status string after a failure
  /// @param[in] writer_error Writer error
  /// @param[in] status_string Human readable status string
  /// @return Returns the writer that was set
  [[nodiscard]] LogError set_writer_error(LogError writer_error, std::string_view status_string);

  /// Set the writer error and status string after a failure without returning the error value
  /// @param[in] writer_error Writer error
  /// @param[in] status_string Human readable status string
  void set_writer_error_no_return(LogError writer_error, std::string_view status_string);

  /// Memory resource used to allocate after initializatin
  jewels::memory::MemoryResource runtime_memory_resource_;

  /// State shared between the writer thread and the thread that reports the writer status
  jewels::memory::pmr_unique_ptr<GuardedState> guarded_state_;

  /// Number of pending async operations
  size_t pending_request_count_{0U};

  /// Current log file offset
  size_t log_file_offset_{0U};

  /// Filesystem library
  jewels::filesystem::Filesystem kits_fs_;

  /// Storage for the queue of outstanding async write request handles
  std::pmr::vector<jewels::memory::AlignedStorage<AsyncWriteRequestHandleType>>
    outstanding_async_write_queue_storage_{};

  /// Circular queue of outstanding async write requests
  jewels::container::CircularBuffer<
    jewels::memory::ObjectPolicy<AsyncWriteRequestHandleType>,
    std::span<jewels::memory::AlignedStorage<AsyncWriteRequestHandleType>, max_async_requests>>
    outstanding_async_write_queue_;

  /// Log directory path
  std::pmr::string log_path_{};

  /// Log file name prefix
  std::pmr::string log_file_prefix_{};

  /// Async request handle pool
  jewels::SharedObjectPool<AsyncRequestHandle> async_request_pool_;

  /// I/O ring
  io_uring ring_{};

  /// Current file descriptor
  jewels::filesystem::FileDescriptor file_desc_{};

  /// Log file sequence number
  uint32_t log_file_sequence_number_{0U};

  /// Flag set when the I/O ring has been initialized
  bool io_ring_initialized_{false};

  /// Writer environment type
  WriterEnvironment writer_environment_;
};

} // namespace clockwork_logging::onboard

#include "clockwork/logging/onboard/async_writer.inl"
