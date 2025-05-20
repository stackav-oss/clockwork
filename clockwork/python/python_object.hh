// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "jewels/meta/concepts.hh"

#include <Python.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace clockwork::python
{

/// Get a string with the current python error
/// @requires The python error indicator must be set
/// @requires The caller must be holding the global interpreter lock
/// @return Python error string
/// @throws Runtime error on failure
[[nodiscard]] std::string get_python_error();

/// RAII Wrapper around a PyObject pointer with utilities for managing python instances
///
/// NOTE: This class is effectively python and throws exceptions on errors.
class PythonObject
{
protected:
  /// Private constructor, use borrow or steal to create a new instance.
  /// @param[in] ptr Python object pointer (maybe be a nullptr)
  explicit PythonObject(PyObject* ptr);

public:
  /// Default constructor creates an instance with a null object pointer
  PythonObject() noexcept = default;

  /// Destructor releases the reference to the python object
  /// @requires The caller must be holding the global interpreter lock if ptr_ is not null
  ~PythonObject();

  /// Copy constructor
  /// @param[in] other Instance to be copied
  /// @requires The caller must be holding the global interpreter lock
  PythonObject(const PythonObject& other);

  /// Copy assignment
  /// @param[in] other Instance to be copied
  /// @requires The caller must be holding the global interpreter lock
  /// @returns A reference to this instance
  PythonObject& operator=(const PythonObject& other);

  /// Move constructor
  /// @param[in,out] other Instance to be moved
  /// @requires The caller must be holding the global interpreter lock
  PythonObject(PythonObject&& other) noexcept;

  /// Move assignment
  /// @param[in,out] other Instance to be moved
  /// @requires The caller must be holding the global interpreter lock
  /// @returns A reference to this instance
  PythonObject& operator=(PythonObject&& other) noexcept;

  /// Reset this instance releasing the reference to the contained object
  void reset();

  /// Release the pointer, the caller is responsible for decrementing the reference count
  /// @return Python object pointer
  [[nodiscard]] PyObject* release();

  /// Python object pointer accessor
  [[nodiscard]] PyObject* get_ptr() const;

  /// Get the reference count on the contained object
  /// @requires The contained object must be valid
  /// @requires The caller must be holding the global interpreter lock
  /// @return Reference count
  [[nodiscard]] size_t get_refcnt() const;

  /// Test the contained python pointer is valid
  /// @return True iff the python pointer is valid
  [[nodiscard]] bool is_valid() const;

  /// Test the contained python pointer is valid
  /// @return True iff the python pointer is valid
  [[nodiscard]] explicit operator bool() const;

  /// Make an object that contains the python None object
  /// @requires The caller must be holding the global interpreter lock
  /// @returns An instance containing python None
  [[nodiscard]] static PythonObject make_none();

  /// Make an object that contains the python True object
  /// @requires The caller must be holding the global interpreter lock
  /// @returns An instance containing python True
  [[nodiscard]] static PythonObject make_true();

  /// Make an object that contains the python False object
  /// @requires The caller must be holding the global interpreter lock
  /// @returns An instance containing python False
  [[nodiscard]] static PythonObject make_false();

  /// Test if the contained object is the python None object
  /// @return True iff the contained object is None
  [[nodiscard]] bool is_none() const;

  /// Test if the contained object is the python True object
  /// @return True iff the contained object is True
  [[nodiscard]] bool is_true() const;

  /// Test if the contained object is the python False object
  /// @return True iff the contained object is False
  [[nodiscard]] bool is_false() const;

  /// Make a python object by borrowing a reference to a python pointer (increments refcnt)
  /// @param[in] ptr Python object pointer, may be nullptr
  /// @return PythonObject instance
  static PythonObject borrow(PyObject* ptr);

  /// Make a python object by stealing a reference to a python pointer (doesn't increment refcnt)
  /// @param[in] ptr Python object pointer, may be nullptr
  /// @return PythonObject instance
  static PythonObject steal(PyObject* ptr);

  /// Import a python module
  /// @param[in] name Module name
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python object encapsulating the imported module dictionary
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject import_module(std::string_view name);

  /// Make an instance containing a python string
  /// @param[in] str String terminated by '\0'
  /// @requires The caller must be holding the global interpreter lock
  /// @returns Python object
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject make_string(std::string_view str);

  /// Get a string from a python object as if calling str(ptr_)
  /// @requires The caller must be holding the global interpreter lock
  /// @returns String extracted from python object
  /// @throws runtime_error on failure
  [[nodiscard]] std::string get_string() const;

  /// Make an instance containing a python integer
  /// @param[in] value Integer value
  /// @requires The caller must be holding the global interpreter lock
  /// @returns Python object
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject make_integer(int64_t value);

  /// Get an integer from a python object
  /// @requires The caller must be holding the global interpreter lock
  /// @returns Integer value
  /// @throws runtime_error on failure
  [[nodiscard]] int64_t get_integer() const;

  /// Make an instance containing a read-only memory view
  /// @param[in] ptr Memory pointer
  /// @param[in] size Memory size
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python memory view object referencing the memory
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject make_read_only_memory_view(const void* ptr, size_t size);

  /// Make an instance containing a writable memory view
  /// @param[in] ptr Memory pointer
  /// @param[in] size Memory size
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python memory view object referencing the memory
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject make_writable_memory_view(void* ptr, size_t size);

  /// Make an instance containing a python tuple
  /// @tparam Args Tuple argument parameter pack
  /// @param[in] args Tuple arguments
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python tuple object containing the arguments
  /// @throws runtime_error on failure
  template <jewels::meta::SameAs<PythonObject>... Args>
  [[nodiscard]] static PythonObject make_tuple(const Args&... args);

  /// Get the number of elements in a python tuple
  /// @requires The caller must be holding the global interpreter lock
  /// @return Number of elements in the python tuple
  /// @throws runtime_error on failure
  [[nodiscard]] size_t get_tuple_size() const;

  /// Get a python tuple element
  /// @param[in] index Python tuple element index
  /// @requires The caller must be holding the global interpreter lock
  /// @return  The tuple element at index
  /// @throws runtime_error on failure
  [[nodiscard]] PythonObject get_tuple_element(size_t index) const;

  /// Make an instance containing a python list from a container of python objects
  /// @tparam ContainerT Container type
  /// @param[in] container Container of python objects
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python list object containing the arguments
  /// @throws runtime_error on failure
  template <typename ContainerT>
  [[nodiscard]] static PythonObject make_list(const ContainerT& container)
    requires std::is_same_v<std::remove_cv_t<typename ContainerT::value_type>, PythonObject>;

  /// Get the number of elements in a python list
  /// @requires The caller must be holding the global interpreter lock
  /// @return Number of elements in the python list
  /// @throws runtime_error on failure
  [[nodiscard]] size_t get_list_size() const;

  /// Access a python list element
  /// @param[in] index Python list element index
  /// @requires The caller must be holding the global interpreter lock
  /// @return  The list element at index
  /// @throws runtime_error on failure
  [[nodiscard]] PythonObject get_list_element(size_t index) const;

  /// Create an empty python dictionary
  /// @return Python dictionary
  /// @throws runtime_error on failure
  [[nodiscard]] static PythonObject make_dictionary();

  /// Get the number of elements in a python dictionary
  /// @requires The caller must be holding the global interpreter lock
  /// @return Number of entries in the python dictionary
  /// @throws runtime_error on failure
  [[nodiscard]] size_t get_dictionary_size() const;

  /// Lookup a dictionary element by name
  /// @param[in] name Item name
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python object from the dictionary or an empty object if the item is not found in the dictionary
  /// @throws runtime_error on failure
  [[nodiscard]] PythonObject get_dictionary_item(std::string_view name) const;

  /// Set a dictionary element
  /// @param[in] name Item name
  /// @param[in] value Value to store in the dictionary
  /// @requires The caller must be holding the global interpreter lock
  /// @throws runtime_error if the element is not found in the dictionary
  void set_dictionary_item(std::string_view name, const PythonObject& value) const;

  /// Invoke a callable python object
  /// @tparam Args Function arguments
  /// @param[in] args Function arguments
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python object returned from the function
  /// @throws runtime_error on failure
  template <jewels::meta::SameAs<PythonObject>... Args>
  PythonObject call_object(const Args&... args) const;

  /// Invoke a python method
  /// @tparam Args Method argument parameter pack
  /// @param[in] name Function name
  /// @param[in] args Function arguments
  /// @requires The caller must be holding the global interpreter lock
  /// @return Python object returned from the function
  /// @throws runtime_error on failure
  template <jewels::meta::SameAs<PythonObject>... Args>
  PythonObject call_method(std::string_view name, const Args&... args) const;

  /// Get a python object attribute
  /// @param[in] name Attribute name
  /// @requires The caller must be holding the global interpreter lock
  /// @return Attribute value
  /// @throws runtime_error on failure
  [[nodiscard]] PythonObject get_attribute(std::string_view name) const;

private:
  /// Python object pointer
  PyObject* ptr_{nullptr};
};

} // namespace clockwork::python

#include "clockwork/python/python_object.inl"
