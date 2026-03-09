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

For cpp language bindings, the strong type can be preserved by using a `cpp` attribute on the `extern` block.

## External strong types

Types outside of the Clockwork DSL can be introduced into the language through use of `cpp` attribute on the `extern` block.

Consider the following example to go with the example above.

```clockwork
#![generate(cpp)]
#![cpp(namespace=clockwork::testing)]

#[cpp(type_namespace=clockwork::external, type_header="path/to/header.hh", type_factory=make_my_strong_type)]
strong_type MyStrongType
{
  underlying_type: Float32;
}
```

The `cpp` attribute on the `strong_type` statement indicates that there is a header file where types are defined in a namespace that match the name of a `strong_type`.
The optional `factory` must refer to a function also defined in that header that takes a single argument matching the `underlying_type` and returning the `strong_type`.
By providing the `factory`, this teaches the DSL how to construct a strong type from the underlying type which enables the use of initial values.
Multiple strong types can be defined in the same source file.
