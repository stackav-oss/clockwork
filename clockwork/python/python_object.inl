// IWYU pragma: private, include "clockwork/python/python_object.hh"
#pragma once

#include "clockwork/python/python_object.hh"

#include "jewels/meta/concepts.hh"

#include <Python.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace clockwork::python
{

template <jewels::meta::SameAs<PythonObject>... Args>
[[nodiscard]] PythonObject PythonObject::make_tuple(const Args&... args)
{
  std::array<PythonObject, sizeof...(args)> args_array = {{args...}};
  auto tuple_obj = steal(PyTuple_New(args_array.size()));
  if (!tuple_obj.is_valid())
  {
    throw std::runtime_error(get_python_error());
  }
  for (size_t i = 0U; i < args_array.size(); ++i)
  {
    PyTuple_SET_ITEM(tuple_obj.get_ptr(), static_cast<Py_ssize_t>(i), args_array.at(i).release());
  }
  return tuple_obj;
}

template <typename ContainerT>
[[nodiscard]] PythonObject PythonObject::make_list(const ContainerT& container)
  requires std::is_same_v<std::remove_cv_t<typename ContainerT::value_type>, PythonObject>
{
  auto list_obj = steal(PyList_New(static_cast<Py_ssize_t>(container.size())));
  if (!list_obj.is_valid())
  {
    throw std::runtime_error(get_python_error());
  }
  for (size_t i = 0U; i < container.size(); ++i)
  {
    PyList_SET_ITEM(list_obj.get_ptr(), static_cast<Py_ssize_t>(i), PythonObject{container.at(i)}.release());
  }
  return list_obj;
}

template <jewels::meta::SameAs<PythonObject>... Args>
PythonObject PythonObject::call_object(const Args&... args) const
{
  PythonObject result;
  if constexpr (sizeof...(args) == 0U)
  {
    result = steal(PyObject_CallNoArgs(ptr_));
  }
  else if constexpr (sizeof...(args) == 1U)
  {
    result = steal(PyObject_CallOneArg(ptr_, std::get<0U>(std::tie(args...)).get_ptr()));
  }
  else
  {
    const auto tuple_obj = make_tuple(args...);
    result = steal(PyObject_CallObject(ptr_, tuple_obj.get_ptr()));
  }
  if (PyErr_Occurred() != nullptr)
  {
    throw std::runtime_error(get_python_error());
  }
  return result;
}

template <jewels::meta::SameAs<PythonObject>... Args>
PythonObject PythonObject::call_method(std::string_view name, const Args&... args) const
{
  const auto method = get_attribute(name);
  return method.call_object(args...);
}

} // namespace clockwork::python
