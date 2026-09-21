// Copyright 2025-2026 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/python/python_object.hh"

#include <Python.h>

#include <cstddef>
#include <stdexcept>

namespace clockwork::python
{

[[nodiscard]] std::string get_python_error()
{
  PyErr_Print();
  return "Internal python error";
}

PythonObject::PythonObject(PyObject* ptr)
  : ptr_(ptr)
{
}

PythonObject::~PythonObject()
{
  reset();
}

[[nodiscard]] PyObject* PythonObject::release()
{
  auto* released_ptr = ptr_;
  ptr_ = nullptr;
  return released_ptr;
}

PythonObject::PythonObject(const PythonObject& other)
  : ptr_(other.ptr_)
{
  if (ptr_ != nullptr)
  {
    Py_IncRef(ptr_);
  }
}

PythonObject& PythonObject::operator=(const PythonObject& other)
{
  if (this != &other)
  {
    reset();
    ptr_ = other.ptr_;
    if (ptr_ != nullptr)
    {
      Py_IncRef(ptr_);
    }
  }
  return *this;
}

PythonObject::PythonObject(PythonObject&& other) noexcept
  : ptr_(other.ptr_)
{
  other.ptr_ = nullptr;
}

PythonObject& PythonObject::operator=(PythonObject&& other) noexcept
{
  if (this != &other)
  {
    reset();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
  }
  return *this;
}

void PythonObject::reset()
{
  if (ptr_ != nullptr)
  {
    Py_DecRef(ptr_);
  }
  ptr_ = nullptr;
}

[[nodiscard]] PyObject* PythonObject::get_ptr() const
{
  return ptr_;
}

[[nodiscard]] size_t PythonObject::get_refcnt() const
{
  if (ptr_ == nullptr)
  {
    throw std::runtime_error("Cannot get refcnt on null object pointer");
  }
  return static_cast<size_t>(Py_REFCNT(ptr_));
}

[[nodiscard]] PythonObject PythonObject::borrow(PyObject* ptr)
{
  if (ptr != nullptr)
  {
    Py_IncRef(ptr);
  }
  return PythonObject{ptr};
}

[[nodiscard]] PythonObject PythonObject::steal(PyObject* ptr)
{
  return PythonObject{ptr};
}

[[nodiscard]] bool PythonObject::is_valid() const
{
  return ptr_ != nullptr;
}

[[nodiscard]] PythonObject::operator bool() const
{
  return is_valid();
}

[[nodiscard]] PythonObject PythonObject::make_true()
{
  return PythonObject::steal(PyBool_FromLong(1));
}

[[nodiscard]] PythonObject PythonObject::make_false()
{
  return PythonObject::steal(PyBool_FromLong(0));
}

[[nodiscard]] bool PythonObject::is_none() const
{
  return Py_IsNone(ptr_);
}

[[nodiscard]] bool PythonObject::is_false() const
{
  return Py_IsFalse(ptr_);
}

[[nodiscard]] bool PythonObject::is_true() const
{
  return Py_IsTrue(ptr_);
}

[[nodiscard]] PythonObject PythonObject::import_module(std::string_view name)
{
  const auto name_obj = make_string(name);
  const auto mod_obj = steal(PyImport_Import(name_obj.get_ptr()));
  if (!mod_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  auto dict_obj = borrow(PyModule_GetDict(mod_obj.get_ptr()));
  return dict_obj;
}

[[nodiscard]] PythonObject PythonObject::make_string(std::string_view str)
{
  auto str_obj = steal(PyUnicode_FromStringAndSize(str.data(), static_cast<Py_ssize_t>(str.size())));
  if (!str_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return str_obj;
}

[[nodiscard]] std::string PythonObject::get_string() const
{
  Py_ssize_t size = 0;
  const auto* str = PyUnicode_AsUTF8AndSize(ptr_, &size);
  if (str == nullptr)
  {
    throw std::runtime_error(get_python_error());
  }
  return std::string{str, static_cast<size_t>(size)};
}

[[nodiscard]] PythonObject PythonObject::make_read_only_memory_view(const void* ptr, size_t size)
{
  auto view_obj = steal(PyMemoryView_FromMemory(
    // Const cast is needed to make the memory view because PyMemoryView has no concept of const memory
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast) See above
    const_cast<char*>(static_cast<const char*>(ptr)),
    static_cast<Py_ssize_t>(size),
    PyBUF_READ));
  if (!view_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return view_obj;
}

[[nodiscard]] PythonObject PythonObject::make_writable_memory_view(void* ptr, size_t size)
{
  auto view_obj = steal(PyMemoryView_FromMemory(static_cast<char*>(ptr), static_cast<Py_ssize_t>(size), PyBUF_WRITE));
  if (!view_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return view_obj;
}

[[nodiscard]] PythonObject PythonObject::make_dictionary()
{
  auto dict_obj = steal(PyDict_New());
  if (!dict_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return dict_obj;
}

[[nodiscard]] size_t PythonObject::get_dictionary_size() const
{
  const auto size = PyDict_Size(ptr_);
  if (size == -1)
  {
    throw std::runtime_error(get_python_error());
  }
  return static_cast<size_t>(size);
}

[[nodiscard]] PythonObject PythonObject::get_dictionary_item(std::string_view name) const
{
  const auto name_obj = make_string(name);
  auto item_obj = borrow(PyDict_GetItemWithError(ptr_, name_obj.get_ptr()));
  if (PyErr_Occurred() != nullptr)
  {
    throw std::runtime_error(get_python_error());
  }
  return item_obj;
}

void PythonObject::set_dictionary_item(std::string_view name, const PythonObject& value) const
{
  const auto name_obj = make_string(name);
  if (PyDict_SetItem(ptr_, name_obj.get_ptr(), value.get_ptr()) != 0)
  {
    throw std::runtime_error(get_python_error());
  }
}

[[nodiscard]] PythonObject PythonObject::get_attribute(std::string_view name) const
{
  auto name_obj = make_string(name);
  auto attr_obj = steal(PyObject_GetAttr(ptr_, name_obj.get_ptr()));
  if (!attr_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return attr_obj;
}

[[nodiscard]] PythonObject PythonObject::make_integer(int64_t value)
{
  auto int_obj = steal(PyLong_FromLongLong(value));
  if (!int_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return int_obj;
}

[[nodiscard]] int64_t PythonObject::get_integer() const
{
  const int64_t int_value = PyLong_AsLongLong(ptr_);
  if (PyErr_Occurred() != nullptr)
  {
    throw std::runtime_error(get_python_error());
  }
  return int_value;
}

[[nodiscard]] size_t PythonObject::get_tuple_size() const
{
  const auto size = PyTuple_Size(ptr_);
  if (size == -1)
  {
    throw std::runtime_error(get_python_error());
  }
  return static_cast<size_t>(size);
}

[[nodiscard]] PythonObject PythonObject::get_tuple_element(size_t index) const
{
  auto element_obj = borrow(PyTuple_GetItem(ptr_, static_cast<Py_ssize_t>(index)));
  if (!element_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return element_obj;
}

[[nodiscard]] size_t PythonObject::get_list_size() const
{
  const auto size = PyList_Size(ptr_);
  if (size == -1)
  {
    throw std::runtime_error(get_python_error());
  }
  return static_cast<size_t>(size);
}

[[nodiscard]] PythonObject PythonObject::get_list_element(size_t index) const
{
  auto element_obj = borrow(PyList_GetItem(ptr_, static_cast<Py_ssize_t>(index)));
  if (!element_obj)
  {
    throw std::runtime_error(get_python_error());
  }
  return element_obj;
}

} // namespace clockwork::python
