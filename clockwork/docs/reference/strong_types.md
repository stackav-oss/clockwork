# User defined strong types

Clockwork has some built in strong types such as `SyncTime` and `Duration`.
Users can also define their own strong types within the domain-specific language (DSL).
This document details how to define a strong type and how to introduce an external strong type.

## Defining a strong type

The following block defines a strong type with name `MyStrongType` that wraps a `Float32`.
Only primitive types are supported as the underlying type.

```clockwork
strong_type MyStrongType
{
  underlying_type: Float32;
}
```

Within the Clockwork language, `MyStrongType` will behave as a strong type.
This means it does not behave like a `Float32`.
For example, you cannot give it an initial value of `1.0`.
The compiler will fail as it does not know how to convert a `Float32` to the strong type.

If used as a schema field, the compiler will lower it to the underlying type when generating language bindings.
In the case above, any c++ bindings will use `float` as the representation for `MyStrongType`.

For cpp language bindings, the strong type can be preserved by coupling it with the `extern` block within a `cpp_target` block.

## External strong types

Types outside of the Clockwork DSL can be introduced into the language through use of the `extern` block in a `cpp_target` block.

Consider the following `cpp_target` to go along with the `strong_type` example above.

```clockwork
cpp_target cpp
{
  options
  {
    namespace clockwork::testing;
  }
  extern
  {
    header_file: "path/to/header.hh";
    namespace: clockwork::external;
    type
    {
      name: MyStrongType;
      factory: make_my_strong_type;
    }
  }
}
```

The `extern` block indicates that there is a header file where types are defined in a namespace that match the name of a `strong_type`.
In the `type` sub-block, `name` must reference a `strong_type` defined both in the DSL and within the `namespace` in the header file.
The optional `factory` must refer to a function also defined in that header that takes a single argument matching the `underlying_type` and returning the `strong_type`.
By providing the `factory`, this teaches the DSL how to construct a strong type from the underlying type which enables the use of initial values.
The `type` sub-block can be repeated within an `extern` block and the `extern` block can be repeated within `cpp_target`.
