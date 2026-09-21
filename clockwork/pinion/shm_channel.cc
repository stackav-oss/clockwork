// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/pinion/shm_channel.hh"

#include "clockwork/pinion/detail/socket_common.hh"
#include "clockwork/pinion/slot_ref.hh"
#include "clockwork/pinion/subscriber_handle.hh"
#include "jewels/container/bounded_string.hh"
#include "jewels/filesystem/error_code.hh"
#include "jewels/filesystem/file.hh"
#include "jewels/log_cerr/log_cerr.hh"
#include "jewels/memory/pointers.hh"

#include <fmt/base.h>
#include <wise_enum.h>

#include <array>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace clockwork::pinion
{
namespace
{

/// Make a new name for a file
/// @param[in] oldfd Old file descriptor
/// @param[in] newdirfd New directory file descriptor
/// @param[in] newpath New file path
/// @param[in] flags Linkat flags
///
/// normal linkat would work for linking a descriptor into the filesystem like this, but that effectively requires root
/// permissions.  So instead this is done by using the procfs symlink
///
/// @return 0 on success, -1 on error.
int linkat_anon(int oldfd, int newdirfd, const char* newpath, int flags)
{
  std::array<char, PATH_MAX> file_path{};
  auto result = fmt::format_to_n(file_path.data(), file_path.size(), "/proc/self/fd/{}", oldfd);
  if (result.size >= file_path.size())
  {
    return ENAMETOOLONG;
  }
  return ::linkat(AT_FDCWD, file_path.data(), newdirfd, newpath, flags);
}

/// Create a shared memory file
/// @param[in] shm_dir Shared memory directory
/// @param[in] name File name
/// @param[in] size File size
/// @return File on success or ShmChannel::Error on failure
jewels::expected<jewels::filesystem::File, ShmChannel::Error>
create_shm_file(const jewels::filesystem::Directory& shm_dir, std::string_view name, size_t size)
{
  // Create the buffer initially as a temporary file to avoid races with subscribers during initialization
  // O_TMPFILE creates an unnamed file in the directory ("/the/given/path/./") which can then be linked back into
  // the filesystem when it's ready
  auto file = jewels::filesystem::File::open(shm_dir.descriptor(), ".", O_TMPFILE | O_RDWR);
  if (!file)
  {
    jewels::log_cerr_error(
      "Failed to create new buffer '{}': {}", name, jewels::filesystem::ErrorCode(file.error().value()));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  // Attempt to set the file/buffer size
  int result = ::ftruncate(file->descriptor(), static_cast<off_t>(size));
  if (result != 0)
  {
    jewels::log_cerr_error("Failed to size new buffer '{}': {}", name, jewels::filesystem::ErrorCode(errno));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  // Use fallocate to reserve the buffer's memory.  Without this, the kernel may overcommit and throw SIGBUS on access
  result = ::posix_fallocate(file->descriptor(), 0, static_cast<off_t>(size));
  if (result != 0)
  {
    jewels::log_cerr_error(
      "Failed to guarantee size of new buffer '{}': {}", name, jewels::filesystem::ErrorCode(result));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  const auto null_terminated_name = jewels::container::BoundedString<NAME_MAX>::try_make(name);
  if (!null_terminated_name)
  {
    jewels::log_cerr_error("Failed to null terminate {} character name '{}'", name.size(), name);
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  // Give the tmpfile a name
  result = linkat_anon(file->descriptor(), shm_dir.descriptor(), null_terminated_name->data(), AT_SYMLINK_FOLLOW);
  if (result == 0)
  {
    // Successfully named the tmpfile, so return it
    return std::move(file.value());
  }
  if (errno == EEXIST)
  {
    // If the target exists then we lost a race.
    jewels::log_cerr_warn("Buffer '{}' was created but suddenly exists, retrying to open", name);
    return jewels::unexpected(ShmChannel::Error::dirty);
  }
  // Link failed in a way that is unrecoverable so abort
  jewels::log_cerr_error("Failed to link new buffer '{}': {}", name, jewels::filesystem::ErrorCode(errno));
  return jewels::unexpected(ShmChannel::Error::fatal);
}

/// Open a shared memory file
/// @param[in] shm_dir Shared memory directory
/// @param[in] name File name
/// @param[in] role Shared memory channel role
/// @param[in] resume_behavior determines whether/how a channel can be reconnected
/// @return File on success or ShmChannel::Error on failure
// NOLINTNEXTLINE(readability-function-cognitive-complexity) TODO OI-2956 Refactor ShmChannel
jewels::expected<jewels::filesystem::File, ShmChannel::Error> open_shm_file(
  const jewels::filesystem::Directory& shm_dir,
  std::string_view name,
  size_t size,
  ShmChannel::Role role,
  ShmChannel::ResumeBehavior resume_behavior)
{
  struct stat statbuf = {};

  const auto null_terminated_name = jewels::container::BoundedString<NAME_MAX>::try_make(name);
  if (!null_terminated_name)
  {
    jewels::log_cerr_error("Failed to null terminate {} character name '{}'", name.size(), name);
    return jewels::unexpected(ShmChannel::Error::fatal);
  }

  while (true)
  {
    const int result = ::fstatat(shm_dir.descriptor(), null_terminated_name->data(), &statbuf, AT_SYMLINK_NOFOLLOW);
    if (result == 0)
    {
      // The file exists, so attempt to use it as-is
      if (role == ShmChannel::Role::publisher && resume_behavior != ShmChannel::ResumeBehavior::dirty_resume)
      {
        return jewels::unexpected(ShmChannel::Error::dirty);
      }
      if ((statbuf.st_mode & S_IFMT) != S_IFREG)
      {
        // While disallowing things like directories or device nodes this will also disallow links to files.
        jewels::log_cerr_error("Failed to open buffer '{}': exists but not a regular file", name);
        return jewels::unexpected(ShmChannel::Error::dirty);
      }
      if (std::cmp_not_equal(statbuf.st_size, size))
      {
        // The on-disk file is an incorrect size which is an error in the overall system state
        jewels::log_cerr_error("Failed to open buffer '{}': on-disk size mismatch", name);
        return jewels::unexpected(ShmChannel::Error::dirty);
      }
      auto file = jewels::filesystem::File::open(
        shm_dir.descriptor(), name, (role == ShmChannel::Role::publisher ? O_RDWR : O_RDONLY));
      if (!file)
      {
        // While there might be some distinction between errors here, broadly this fits the definition of "dirty" in the
        // the file exists but cannot be resumed, even if it's, e.g. a permission error.
        jewels::log_cerr_error(
          "Failed to open buffer '{}': {}", name, jewels::filesystem::ErrorCode(file.error().value()));
        return jewels::unexpected(ShmChannel::Error::dirty);
      }
      return std::move(file.value());
    }
    if (result == -1 && errno == ENOENT)
    {
      // Stat couldn't find the file, so try to create it if this is meant to be a publisher
      if (role != ShmChannel::Role::publisher)
      {
        // Because shm_dir represents and open directory, that means the directory exists and ENOENT is referring to the
        // file only.
        return jewels::unexpected(ShmChannel::Error::missing);
      }
      // Attempt to create the file
      auto file = create_shm_file(shm_dir, name, size);
      // Here dirty means the the file was created by another process while this was creating it, but that doesn't
      // necessarily mean that the system state is dirty (which requires an existing _incompatible_ buffer) so loop to
      // determine if this is actually dirty or just a duplicate publisher.
      if (file == jewels::unexpected(ShmChannel::Error::dirty))
      {
        continue;
      }
      return file;
    }
    // Failed to stat the file in an unrecoverable way
    jewels::log_cerr_error("Failed to stat buffer '{}': {}", name, jewels::filesystem::ErrorCode(errno));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
}

} // namespace

jewels::expected<std::tuple<jewels::filesystem::MMapRegion, ShmChannel::BufferPtr>, ShmChannel::Error>
ShmChannel::open_buffer(
  jewels::memory::MemoryResource memres,
  const jewels::filesystem::Directory& shm_dir,
  std::string_view filename,
  const BufferLayout& layout,
  Role role,
  ShmChannel::ResumeBehavior resume_behavior)
{
  const size_t size = buffer_size(layout);

  auto file = open_shm_file(shm_dir, filename, size, role, resume_behavior);
  if (!file)
  {
    return jewels::unexpected(file.error());
  }
  auto map = jewels::filesystem::MMapRegion::create(
    file->descriptor(), size, (role == Role::publisher ? PROT_WRITE | PROT_READ : PROT_READ), MAP_SHARED);
  if (!map)
  {
    jewels::log_cerr_error("Failed to mmap buffer '{}': {}", filename, jewels::filesystem::ErrorCode(map.error()));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  auto expected_buffer = Buffer::try_make(map->to_span(), layout);
  if (!expected_buffer)
  {
    jewels::log_cerr_error("Failed to make buffer: {}", wise_enum::to_string(expected_buffer.error()));
    return jewels::unexpected(ShmChannel::Error::fatal);
  }
  auto buffer = jewels::memory::make_pmr_unique<Buffer>(memres, *expected_buffer);
  return std::make_tuple(std::move(*map), std::move(buffer));
}

jewels::expected<UnixSocket, ShmChannel::Error>
ShmChannel::open_socket(std::string_view socket_ns, std::string_view name, Role role)
{
  // UnixSocket will validate against the real max length so this is just something large enough
  constexpr size_t path_max_size = 200;
  jewels::container::BoundedString<path_max_size> path;
  if (!path.try_concat(socket_ns) || !path.try_concat("/") || !path.try_concat(name))
  {
    jewels::log_cerr_error("Failed to create socket '{}'+'{}' name too long", socket_ns, name);
    return jewels::unexpected(Error::fatal);
  }
  auto sock = (role == Role::publisher ? UnixSocket::create_bind(path) : UnixSocket::create_connect(path));
  if (!sock)
  {
    if (role == Role::publisher && sock.error() == std::errc::address_in_use)
    {
      // The address is in use which probably means that a publisher already exists.  This could also be considered
      // `dirty` but it probably results from a fatal configuration error rather than a failure to properly clean
      // the artifacts of a previous run so its considered fatal.
      jewels::log_cerr_error("Failed to create socket '{}': another publisher already exists", path.to_string_view());
      return jewels::unexpected(Error::fatal);
    }
    if ((role == Role::subscriber) && sock.error() == std::errc::connection_refused)
    {
      // This should mean that no publisher is listening, which maps to "missing"
      return jewels::unexpected(Error::missing);
    }
    jewels::log_cerr_error(
      "Failed to create socket '{}': {}", path.to_string_view(), jewels::filesystem::ErrorCode(sock.error()));
    return jewels::unexpected(Error::fatal);
  }
  if (!set_nonblocking(sock->descriptor(), true))
  {
    return jewels::unexpected(Error::fatal);
  }
  return std::move(*sock);
}

void ShmChannel::close_socket()
{
  socket_.close();
}

// NOLINTNEXTLINE(readability-function-size): TODO OI-2956 Refactor ShmChannel into better interfaces and tools
ShmChannel::ShmChannel(
  jewels::memory::MemoryResource memres,
  BufferPtr buffer,
  jewels::filesystem::MMapRegion map,
  UnixSocket socket,
  std::string_view socket_ns,
  std::string_view filename,
  std::string_view channel_name,
  ResumeBehavior resume_behavior)
  : map_(std::move(map)),
    buffer_(std::move(buffer)),
    socket_(std::move(socket)),
    socket_ns_(socket_ns, memres),
    filename_(filename, memres),
    channel_name_(channel_name, memres),
    resume_behavior_(resume_behavior)
{
  // Some internal functions need to misbehave for this to happen
  if (!buffer_)
  {
    throw std::runtime_error("ShmChannel created with null buffer");
  }
}

ShmChannel::~ShmChannel() = default;

const BufferLayout& ShmChannel::layout() const noexcept
{
  return buffer_->layout();
}

std::ranges::subrange<SlotRef> ShmChannel::available() const
{
  auto buffer = jewels::memory::make_non_null_from_ref(*buffer_);
  // Assigning to variables to make evaluation order explicit because
  // for this range calling begin() and end() use atomic operations.
  // Begin should be called before end. Worst case, publisher adds new
  // messages causing begin to update between the calls of begin and
  // end.  This same thing can also happen after this function
  // returns.  Users should always check still_available to make sure
  // data is valid regardless.
  const auto begin = SlotRef(buffer, std::begin(*buffer));
  const auto end = SlotRef(buffer, std::end(*buffer));
  return std::ranges::subrange<SlotRef>{begin, end};
}

size_t ShmChannel::get_publish_count() const noexcept
{
  return buffer()->get_publish_count();
}

SubscriberHandle ShmChannel::make_subscriber()
{
  return SubscriberHandle{jewels::memory::make_non_null_from_ref(*buffer_)};
}

int ShmChannel::socket() const noexcept
{
  return socket_.descriptor();
}

bool ShmChannel::handshake()
{
  return true;
}

void ShmChannel::set_socket(UnixSocket socket) noexcept
{
  socket_ = std::move(socket);
}

jewels::memory::ObjectPtr<Buffer> ShmChannel::buffer() const noexcept
{
  return jewels::memory::make_non_null_from_ref(*buffer_);
}

[[nodiscard]] const std::pmr::string& ShmChannel::socket_ns() const noexcept
{
  return socket_ns_;
}

[[nodiscard]] const std::pmr::string& ShmChannel::scope() const noexcept
{
  return socket_ns_;
}

[[nodiscard]] const std::pmr::string& ShmChannel::filename() const noexcept
{
  return filename_;
}

[[nodiscard]] const std::pmr::string& ShmChannel::identifier() const noexcept
{
  return filename_;
}

[[nodiscard]] const std::pmr::string& ShmChannel::channel_name() const noexcept
{
  return channel_name_;
}

[[nodiscard]] const std::pmr::string& ShmChannel::name() const noexcept
{
  return channel_name_;
}

ShmChannel::ResumeBehavior ShmChannel::resume_behavior() const noexcept
{
  return resume_behavior_;
}

} // namespace clockwork::pinion
