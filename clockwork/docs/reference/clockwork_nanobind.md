# Clockwork nanobind generation

Clockwork can expose C++ types to Python using nanobind.
This lets you efficiently call C++ functions and load logs.

## TL;DR - How to access C++ types from Python

For a module `demo/foo.clk`:

1. Add a `nanobind_target` to your `.clk` module.
   List the types you want to expose.

   ```clockwork
   // demo/foo.clk

   ...

   nanobind_target foo_clk_nb
   {
     constant some_constant;
     binding SomeEnum;
     binding Tappy<SomeMessage>;
     binding SomeGenericMessageFloat32: Tappy<SomeGenericMessage<Float32>>;
   }
   ```

   The target name must end in `_clk_nb`.

2. Run `clk-deps` (and `fix-viz` if needed) to update the `BUILD.bazel` targets

There is now a python library `//demo:foo_clk_nb` that you can import normally.

> [!TIP]
> You may need to update the `visibility` field to make the python library `//demo:foo_clk_nb` visible from your source file.

### Calling C++ functions

To call a C++ function `my_function(Tappy<SomeMessage>&)` that uses your bound types, write a `py_cc_binding` target as normal.

```python
# demo/BUILD.bazel

py_cc_binding(
  name = "bar",
  srcs = ["nb_bar.cc"],
  py_deps = ["//demo:foo_clk_nb"],  # the clockwork nanobind library
  deps = ["//demo:foo_clk_cc"],     # the clockwork C++ library
)
```

Then, write `nb_bar.cc` as:

```c++
// demo/nb_bar.cc

NB_MODULE(nb_bar, mod)
{
  // import clockwork nanobind module
  nb::module_::import_("demo.foo_clk_nb");

  // bind your C++ function
  mod.def("my_function", &my_function);
}
```

You can now import and call your function from Python.

```python
from demo.foo_clk_nb import SomeMessage
from demo.bar import my_function

msg = SomeMessage()
print(my_function(msg))
```

## C++/Python type mapping

### Clockwork schemas

Clockwork schemas are exposed as Python classes.
They have all the fields defined in their schemas.

They have additional methods of `clockwork.serialization.py.protocol.Tachyon`, which lets them be serialized to/from bytes.

These classes can be efficiently pickled.

### Clockwork enums

Enums become first class Python enums (`enum.Enum`).

### SyncTime / Duration

The C++ time types are mapped as

- `jewels::time::SyncTime` -> `jewels.nanobind.nb_sync_time.SyncTime`
- `chrono::duration` -> `jewels.nanobind.nb_sync_time.Duration`

This is non-standard.
By default, nanobind maps time types to Python `datetime.datetime`/`datetime.timedelta`.
We override this behavior because those Python types are not ergonomic for the way Stack uses `SyncTime`.
They also only capture microsecond precision, which means they're lossy representations.

To migrate legacy code for a bindings module:

- remove `#include <nanobind/stl/chrono.h>`
- call `nanobind::module_::import_("jewels.nanobind.nb_sync_time");`
- add `"@clockwork//jewels/nanobind:nb_sync_time"` to your `py_cc_binding`'s `py_deps

If you've enabled typing for your module, Pyright will then help you migrate the user code.

### UUID

C++ `jewels::UUID` is mapped to Python `uuid.uuid`.

### Aurora units

Aurora units are represented in Python by their underlying types, which are usually Python `float`.

You may need to `#include <jewels/nanobind/nb_au.hh.h>  // IWYU pragma: keep` to user bindings modules to utilize this feature.

### Arrays

Clockwork `FixedArray` and `VarArray` are not mapped to Python lists, they are first-class bound C++ types.
They are highly compatible with lists, and can be converted to lists by calling `list(my_array)`.

Lists can be directly assigned to array fields, and they will be implicitly converted.
Conversion with throw errors if list length is larger than `VarArray` capacity, or not equal to `FixedArray` size.
For operations such as equality checks, lists will be compared to arrays element by element without conversion, with appropriate length checking.

```Python
foo = Foo()
my_array = Foo().some_array  # returns VarArray_SubMessage_20 | list[SubMessage], but it's never actually a list
assert isinstance(my_array, VarArray_SubMessage_20)  # always True
some_list: list[SubMessage] = [...]
foo.some_array = some_list  # implicitly converts from list[SubMessage] to VarArray_SubMessage_20
my_array == some_list  # compares VarArray to list element-wise
```

#### Arrays have mutable reference semantics

Getting an array from a message returns a reference.
Getting an element from an array returns a reference.

```Python
foo = Foo()                  # schema type
my_array = Foo().some_array  # returns VarArray_SubMessage_20
sub_msg = my_array[0]        # get a reference to an array element
sub_msg.some_field = 22      # mutates 'my_array' and 'Foo'
```

This matches the semantics of Python dataclasses.

#### Numpy array compatibility

`FixedArray` and `VarArray` bindings internally define `__array__` and are compatible with `numpy` arrays.
This means that they are accepted by libraries such as `matplotlib` and `plotly`.

### Optional\<>

Clockwork `Optional<T>` types show up in Python as `T | None`.

#### Optionals have mutable reference semantics

Accessing an optional field from a message returns either `None`, or a reference to the underlying field.

```Python
foo = Foo()                                # schema type
maybe_sub_msg = Foo().some_optional_field  # returns 'SubMessage | None'
if maybe_sub_msg is None:                  # check if field is set
    # do something
    ...
maybe_submsg.some_field = 22               # mutates Foo
```

This matches the semantics of Python dataclasses.

### Primitives

All integer types `UInt8`/`UInt16`/`UInt32`/`UInt64`/`Int8`/`Int16`/`Int32`/`Int64` are mapped to the python `int` type.

`Float64`/`Float32` are both mapped to the Python `float` type.

### VarString

`VarString` shows up as the normal Python `str` type.
An exception will be raised if you assign a too-large `str` to a clockwork `VarString` field.

### Exceptions to mutable reference semantics

When types must be copied, mutability semantics diverge from normal python dataclass/list mutability semantics.

Consider:

```Python
submsg = SubMessage()

foo = Foo()  # schema type
foo.some_array[0] = submsg  # assign a message to an element of the array
foo.some_array[0].field = 22

assert submsg.field == 22  # this fails, because submsg was copied to the array
```

In this example, a `SubMessage` is assigned to an array element.
Because this is a C++ array of values, the message _must_ be copied, and updates to the array element can't affect the original data.

This applies to `Optional`, `FixedArray`, and `VarArray` containers.

### Return by reference is supported

Python can call C++ functions that return outputs by mutable reference, as well as return by value.

```c++
void my_function(const Input& input, Output& output);
```

```Python
inputs = Inputs(field_a=..., field_b=...)  # set inputs
outputs = Outputs()  # default constructor
my_function(inputs, outputs)  # outputs are mutated in-place
```

## Addendum - Example Clockwork module

Here is an example of a full Clockwork module containing C++ and nanobind targets:

```clk
some_constant: Int32 = 42;

schema SomeMessage
{
  ...
}

schema SomeGenericMessage
{
  parameters
  {
    ...
  }
  ...
}

enum SomeEnum
{
  ...
}

cpp_target foo_clk_cc
{
  constant some_constant;
  enum SomeEnum;
  schema SomeMessage;
  schema SomeGenericMessage;

  representation Tachyon<SomeMessage>;
  interface Tappy<SomeMessage>;

  representation Tachyon<SomeGenericMessage<Float32>>;
  interface Tappy<SomeGenericMessage<Float32>>;
}

nanobind_target foo_clk_nb
{
  constant some_constant;
  binding SomeEnum;
  binding Tappy<SomeMessage>;
  binding SomeGenericMessageFloat32: Tappy<SomeGenericMessage<Float32>>;
}
```

This Clockwork module will result in the following Bazel targets:

```python
clk(
  name = "foo_clk",
  srcs = ["foo.clk"],
  outs = [
    "foo_clk_cc.cc",
    "foo_clk_cc.inl",
    "foo_clk_cc.hh",
    "foo_clk_nb.cc",
  ],
)

cc_library(
    name = "foo_clk_cc",
    srcs = ["foo_clk_cc.cc", "foo_clk_cc.inl"],
    hdrs = ["foo_clk_cc.hh"],
)

py_cc_binding(
    name = "foo_clk_nb",
    srcs = ["foo_clk_nb.cc"],
    deps = [
        ":foo_clk_cc",
        "@nanobind",
    ],
)
```

Most of this `BUILD.bazel` file can be autogenerated by running `clk-deps`.
