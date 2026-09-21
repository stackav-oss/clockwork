# Tap interfaces

This document covers the Tap interface for schemas.
The goal of Tap is to encapsulate the Tachyon representation of a schema behind consistent APIs.
In the current implementation, not all APIs are completely encapsulated.
However, long term, full encapsulation is the plan where the layout can be swapped but the APIs remain the same.
See documentation on [representations and interfaces](repr_iface.md).

## Interfaces in C++

Consider the following schema:

```clockwork
#![generate(cpp)]
#![cpp(namespace=clockwork)]

schema MySchema
{
  fields
  {
    #1 field: Type;
  }
}
```

`MySchema` is a schema with exactly one field named `field` of type `Type`.
The Clockwork compiler will generate a `Tachyon` representation and a `Tap` interface for every schema defined in the file.
The APIs provided by `Tap` are dependent on `Type` (the type of `field`).
The following sections will provide the methods that will be generated depending on the field type.

### Non-container types

Non-container types include primitives, enums, UUIDs, etc.
All of these types provide the same set of APIs.

| Method name         | Return type   | Arguments | Description                       | Side Effects |
| ------------------- | ------------- | --------- | --------------------------------- | ------------ |
| `get_field`         | `const Type&` | None      | Gets the value by const reference | None         |
| `get_mutable_field` | `Type&`       | None      | Gets value by mutable reference   | None         |
| `set_field`         | None          | `Type`    | Sets the value.                   | None         |

### `FixedArray`

`FixedArrary` is a fixed size array.
This is akin to `std::array`.
The API of `FixedArray` encourages the use of spans for access to the data.
Assuming `Type` is `FixedArray<type=ValueType, size=N>`, the following APIs are provided.

| Method name         | Return type                     | Arguments                       | Description                                     | Side Effects |
| ------------------- | ------------------------------- | ------------------------------- | ----------------------------------------------- | ------------ |
| `get_field`         | `std::span<const ValueType, N>` | None                            | Gets a fixed-size const span to the elements.   | None         |
| `get_mutable_field` | `std::span<ValueType, N>`       | None                            | Gets a fixed-size mutable span to the elements. | None         |
| `set_field`         | None                            | `std::span<const ValueType, N>` | Sets values from a fixed-size const span.       | None         |

### `Tensor`

`Tensor` is a multi-dimensional array.
Assuming `Type` is `Tensor<ValueType, shape=[x, y, z], layout=[a, b, c]>`, the number of dimensions is `N`, and the product of the elements of `shape` is `P`, the following APIs are provided.

| Method name         | Return type                                                            | Arguments                       | Description                               | Side Effects |
| ------------------- | ---------------------------------------------------------------------- | ------------------------------- | ----------------------------------------- | ------------ |
| `get_field`         | `const jewels::tap::Tensor<ValueType, Sizes<x, y,z>, Sizes<a, b, c>>&` | None                            | Gets an immutable handle to the tensor.   | None         |
| `get_mutable_field` | `jewels::tap::Tensor<ValueType, Sizes<x, y,z>, Sizes<a, b, c>>&`       | None                            | Gets a mutable handle to the tensor.      | None         |
| `set_field`         | None                                                                   | `std::span<const ValueType, P>` | Sets values from a fixed-size const span. | None         |

`jewels::tap::Tensor` wraps the underlying buffer for the tensor and all of the type information necessary to construct a richer interface over the buffer before doing useful work with it.
Its API is described in the table below.

| Method name      | Return type                     | Arguments | Description                                                    | Side Effects |
| ---------------- | ------------------------------- | --------- | -------------------------------------------------------------- | ------------ |
| `storage`        | `std::span<ValueType, P>`       | None      | Gets a flat mutable span over this tensor's backing storage.   | None         |
| `storage`        | `std::span<const ValueType, P>` | None      | Gets a flat immutable span over this tensor's backing storage. | None         |
| `num_dimensions` | `size_t`                        | None      | Gets the number of dimensions comprised by the tensor.         | None         |
| `shape`          | `std::array<size_t, N>`         | None      | Gets the array of dimension sizes for the tensor.              | None         |
| `layout`         | `std::array<size_t, N>`         | None      | Gets the array of per-dimension strides for the tensor.        | None         |

For more details, see `jewels/tap/tensor.hh`.

### `VarArray`

`VarArray` is a fixed capacity array.
Think of a `std::vector` but the underlying buffer is a `std::array` or for those familiar with boost's containers this is akin to `boost::static_vector`.
Similar to `FixedArray`, the API of `VarArray` encourages the use of spans for access to the data.
However, resizing the container requires breaking encapsulation to access the underling `::stack::tap::VarArray` container type.
Eventually, access to this underlying type will be removed and an alternate mutation API will be provided.
Assuming `Type` is `VarArray<type=ValueType, max_size=N>`, the following APIs are provided.

| Method name            | Return type                  | Arguments | Description                          | Side Effects |
| ---------------------- | ---------------------------- | --------- | ------------------------------------ | ------------ |
| `get_field`            | `std::span<const ValueType>` | None      | Gets a const span to the elements.   | None         |
| `get_mutable_field`    | `std::span<ValueType>`       | None      | Gets a mutable span to the elements. | None         |
| `get_underlying_field` | `VarArray<ValueType, N>&`    | None      | Gets the underlying container type.  | None         |

### `VarString`

Similar to `VarArray`, `VarString` is a fixed capacity string.
The APIs have a similar theme as `VarArray`.
Assuming `Type` is `VarString<max_size=N>`, the following APIs are provided.

| Method name            | Return type        | Arguments | Description                            | Side Effects |
| ---------------------- | ------------------ | --------- | -------------------------------------- | ------------ |
| `get_field`            | `std::string_view` | None      | Gets a string view.                    | None         |
| `get_mutable_field`    | `std::span<char>`  | None      | Gets a mutable span to the characters. | None         |
| `get_underlying_field` | `VarString<N>&`    | None      | Gets the underlying string type.       | None         |

### `Optional`

`Optional` can hold one value or nothing.
Accessing the value of an optional requires the value exists.
The access method is prefixed with `value_` instead of `get_` to differentiate this behavior compared to other types and works similarly to `std::optional::value()`
Assuming `Type` is `Optional<ValueType>`, the following APIs are provided.

| Method name           | Return type        | Arguments          | Description                                              | Side Effects               |
| --------------------- | ------------------ | ------------------ | -------------------------------------------------------- | -------------------------- |
| `value_field`         | `const ValueType&` | None               | Gets the value from the optional as a const reference.   | Throws if no value exists. |
| `value_mutable_field` | `ValueType&`       | None               | Gets the value from the optional as a mutable reference. | Throws if no value exists. |
| `set_field`           | None               | `const ValueType&` | Sets the optional to a specific value.                   | None                       |
| `has_field`           | `bool`             | None               | Checks if a value exists.                                | None                       |
| `reset_field`         | `void`             | None               | Resets the optional to an empty state.                   | None                       |

### Schema constructors

All schemas are default constructible.
There is only one additional constructor supported.
The constructor can be added using the `options` block.

```clockwork
schema SchemaWithConstructor
{
  options
  {
    constructor: source_code_order;
  }
  fields
  {
    ...
  }
}
```

Enabling the constructor does two things.
One, it generates an additional struct `TapInit<Tachyon<SchemaWithConstructor>>`.
That init struct has the schema fields in the order they appear in the `.clk` source file.
Two, it provides a constructor for `Tap<Tachyon<SchemaWithConstructor>>` that takes in the init struct.

It is recommended to invoke that constructor using named initializers.
Eventually a compiler error will be enabled (requires an upgrade) to fail compilation if a field is missed.

```cpp
Tap<Tachyon<SchemaWithConstructor>> var{{.field_a = var_a, .field_b = var_b, ...}};
```

This avoids the need to explicitly write out `TapInit<Tachyon<SchemaWithConstructor>>` and is as close as we can get to initializing a struct using named initializers.

> [!WARNING]
> Do not default construct a `TapInit` type (e.g., `TapInit<Tachyon<T>>{}`).
> These structs intentionally leave fields uninitialized.
> This is because `-Wmissing-field-initializers` won't flag a missing member if it has explicit initialization in the member declaration.
> The consequence of this is primitive types may be left in an uninitialized state if a `TapInit` type is default constructed.
