// IWYU pragma: private, include "jewels/aligner/aligner.hh"

#pragma once

#include "jewels/aligner/aligner.hh"

#include <cstddef>
#include <type_traits>

namespace jewels
{

template <size_t alignment>
template <typename OffsetType>
[[nodiscard]] constexpr bool Aligner<alignment>::is_aligned(OffsetType offset)
  requires(std::is_integral_v<OffsetType>)
{
  if constexpr (alignment == 1U)
  {
    return true;
  }

  if constexpr (std::is_signed_v<OffsetType>)
  {
    if (offset < 0)
    {
      return is_aligned_impl(static_cast<size_t>(-offset));
    }
  }

  return is_aligned_impl(static_cast<size_t>(offset));
}

template <size_t alignment>
template <typename OffsetType>
[[nodiscard]] constexpr OffsetType Aligner<alignment>::align_prev(OffsetType offset)
  requires(std::is_integral_v<OffsetType>)
{
  if constexpr (alignment == 1U)
  {
    return offset;
  }

  if constexpr (std::is_signed_v<OffsetType>)
  {
    if (offset < 0)
    {
      return -static_cast<OffsetType>(align_next_impl(static_cast<size_t>(-offset)));
    }
  }

  return static_cast<OffsetType>(align_prev_impl(static_cast<size_t>(offset)));
}

template <size_t alignment>
template <typename OffsetType>
[[nodiscard]] constexpr OffsetType Aligner<alignment>::align_next(OffsetType offset)
  requires(std::is_integral_v<OffsetType>)
{
  if constexpr (alignment == 1U)
  {
    return offset;
  }

  if constexpr (std::is_signed_v<OffsetType>)
  {
    if (offset < 0)
    {
      return -static_cast<OffsetType>(align_prev_impl(static_cast<size_t>(-offset)));
    }
  }

  return static_cast<OffsetType>(align_next_impl(static_cast<size_t>(offset)));
}

template <size_t alignment>
template <typename OffsetType>
[[nodiscard]] constexpr OffsetType Aligner<alignment>::aligned_offset(OffsetType offset)
  requires(std::is_integral_v<OffsetType>)
{
  if constexpr (alignment == 1U)
  {
    return 0U;
  }

  if constexpr (std::is_signed_v<OffsetType>)
  {
    if (offset < 0)
    {
      return static_cast<OffsetType>(aligned_remainder_impl(static_cast<size_t>(-offset)));
    }
  }

  return static_cast<OffsetType>(aligned_offset_impl(static_cast<size_t>(offset)));
}

template <size_t alignment>
template <typename OffsetType>
[[nodiscard]] constexpr OffsetType Aligner<alignment>::aligned_remainder(OffsetType offset)
  requires(std::is_integral_v<OffsetType>)
{
  if constexpr (alignment == 1U)
  {
    return 0U;
  }

  if constexpr (std::is_signed_v<OffsetType>)
  {
    if (offset < 0)
    {
      return static_cast<OffsetType>(aligned_offset_impl(static_cast<size_t>(-offset)));
    }
  }

  return static_cast<OffsetType>(aligned_remainder_impl(static_cast<size_t>(offset)));
}

template <size_t alignment>
template <typename T>
[[nodiscard]] bool Aligner<alignment>::ptr_is_aligned(const T* ptr)
  requires(sizeof(T) == 1U)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Alignment check requires integral pointer value
  return is_aligned(reinterpret_cast<ptrdiff_t>(ptr));
}

template <size_t alignment>
template <typename T>
[[nodiscard]] ptrdiff_t Aligner<alignment>::ptr_aligned_offset(const T* ptr)
  requires(sizeof(T) == 1U)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Alignment check requires integral pointer value
  return aligned_offset(reinterpret_cast<ptrdiff_t>(ptr));
}

template <size_t alignment>
template <typename T>
[[nodiscard]] ptrdiff_t Aligner<alignment>::ptr_aligned_remainder(const T* ptr)
  requires(sizeof(T) == 1U)
{
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Alignment check requires integral pointer value
  return aligned_remainder(reinterpret_cast<ptrdiff_t>(ptr));
}

template <size_t alignment>
[[nodiscard]] constexpr bool Aligner<alignment>::is_aligned_impl(size_t offset)
{
  return (offset & alignment_mask) == offset;
}

template <size_t alignment>
[[nodiscard]] constexpr size_t Aligner<alignment>::align_prev_impl(size_t offset)
{
  return offset & alignment_mask;
}

template <size_t alignment>
[[nodiscard]] constexpr size_t Aligner<alignment>::align_next_impl(size_t offset)
{
  return (offset + alignment - 1U) & alignment_mask;
}

template <size_t alignment>
[[nodiscard]] constexpr size_t Aligner<alignment>::aligned_offset_impl(size_t offset)
{
  return offset - (offset & alignment_mask);
}

template <size_t alignment>
[[nodiscard]] constexpr size_t Aligner<alignment>::aligned_remainder_impl(size_t offset)
{
  return ((offset + alignment - 1U) & alignment_mask) - offset;
}

} // namespace jewels
