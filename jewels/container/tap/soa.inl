// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0
// IWYU pragma: private, include "jewels/container/tap/soa.hh"

#pragma once

#include "jewels/container/tap/soa.hh"

#include "jewels/callsig/outcome.hh"
#include "jewels/callsig/outparam.hh"

#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace jewels::tap
{

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline auto SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::operator[](size_t index) noexcept
  -> ElementRef
{
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline auto SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::operator[](size_t index) const noexcept
  -> ElementConstRef
{
  return ElementConstRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::at(
  jewels::FactoryOut<ElementRef> element_out, size_t index) noexcept
{
  if (index >= size())
  {
    return jewels::failure;
  }
  element_out->emplace(&derived(), index);
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::at(
  jewels::FactoryOut<ElementConstRef> element_out, size_t index) const noexcept
{
  if (index >= size())
  {
    return jewels::failure;
  }
  element_out->emplace(&derived(), index);
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::at(size_t index)
{
  if (index >= size())
  {
    throw std::out_of_range{"SoaInterface::at: index out of range"};
  }
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementConstRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::at(size_t index) const
{
  if (index >= size())
  {
    throw std::out_of_range{"SoaInterface::at: index out of range"};
  }
  return ElementConstRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline size_t SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::size() const noexcept
{
  if constexpr (is_variable)
  {
    return derived().size_;
  }
  else
  {
    return fixed_capacity;
  }
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
constexpr size_t SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::capacity() const noexcept
{
  return fixed_capacity;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline bool SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::empty() const noexcept
{
  return size() == 0;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline void SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::resize(size_t new_size)
  requires(is_variable)
{
  if (new_size > capacity())
  {
    throw std::length_error{"VarSoa resize: new size exceeds capacity"};
  }
  const auto old_size = std::exchange(derived().size_, new_size);
  if (old_size < new_size)
  {
    derived().construct_range(old_size, new_size);
  }
  else if (old_size > new_size)
  {
    derived().wipe_range(new_size, old_size);
  }
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::try_resize(size_t new_size) noexcept
  requires(is_variable)
{
  if (new_size > capacity())
  {
    return jewels::failure;
  }
  const auto old_size = std::exchange(derived().size_, new_size);
  if (old_size < new_size)
  {
    derived().construct_range(old_size, new_size);
  }
  else if (old_size > new_size)
  {
    derived().wipe_range(new_size, old_size);
  }
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline void SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::clear()
  requires(is_variable)
{
  resize(0);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::emplace_back()
  requires(is_variable)
{
  if (size() >= capacity())
  {
    throw std::length_error{"VarSoa emplace_back: size exceeds capacity"};
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index);
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::emplace_back(
  const clockwork::TapInit<ElementType>& element)
  requires(is_variable)
{
  if (size() >= capacity())
  {
    throw std::length_error{"VarSoa emplace_back: size exceeds capacity"};
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::emplace_back(const ElementType& element)
  requires(is_variable)
{
  if (size() >= capacity())
  {
    throw std::length_error{"VarSoa emplace_back: size exceeds capacity"};
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::ElementRef
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::emplace_back(const ElementRef& element)
  requires(is_variable)
{
  if (size() >= capacity())
  {
    throw std::length_error{"VarSoa emplace_back: size exceeds capacity"};
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  return ElementRef(&derived(), index);
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::try_emplace_back(
  jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out) noexcept
  requires(is_variable)
{
  if (size() >= capacity())
  {
    return jewels::failure;
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index);
  if (element_out.has_value())
  {
    element_out->emplace(&derived(), index);
  }
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::try_emplace_back(
  const clockwork::TapInit<ElementType>& element,
  jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out) noexcept
  requires(is_variable)
{
  if (size() >= capacity())
  {
    return jewels::failure;
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  if (element_out.has_value())
  {
    element_out->emplace(&derived(), index);
  }
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::try_emplace_back(
  const ElementType& element, jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out) noexcept
  requires(is_variable)
{
  if (size() >= capacity())
  {
    return jewels::failure;
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  if (element_out.has_value())
  {
    element_out->emplace(&derived(), index);
  }
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline jewels::BinaryOutcome SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::try_emplace_back(
  const ElementRef& element, jewels::OptionalOut<jewels::FactoryResult<ElementRef>> element_out) noexcept
  requires(is_variable)
{
  if (size() >= capacity())
  {
    return jewels::failure;
  }
  const auto index = size();
  derived().size_++;
  derived().construct_element(index, element);
  if (element_out.has_value())
  {
    element_out->emplace(&derived(), index);
  }
  return jewels::success;
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::begin() noexcept
{
  return iterator{&derived(), 0};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::end() noexcept
{
  return iterator{&derived(), size()};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::begin() const noexcept
{
  return const_iterator{&derived(), 0};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::end() const noexcept
{
  return const_iterator{&derived(), size()};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::cbegin() const noexcept
{
  return const_iterator{&derived(), 0};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::cend() const noexcept
{
  return const_iterator{&derived(), size()};
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::rbegin() noexcept
{
  return std::make_reverse_iterator(end());
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::rend() noexcept
{
  return std::make_reverse_iterator(begin());
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::rbegin() const noexcept
{
  return std::make_reverse_iterator(end());
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::rend() const noexcept
{
  return std::make_reverse_iterator(begin());
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::crbegin() const noexcept
{
  return std::make_reverse_iterator(cend());
}

template <class Derived, class ElementType, size_t fixed_capacity, bool is_variable>
inline typename SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::const_reverse_iterator
SoaInterface<Derived, ElementType, fixed_capacity, is_variable>::crend() const noexcept
{
  return std::make_reverse_iterator(cbegin());
}

} // namespace jewels::tap
