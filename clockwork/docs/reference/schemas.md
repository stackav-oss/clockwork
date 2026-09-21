# Clockwork schema language reference manual

This document covers the details of the schema-related features of the Clockwork language.

## Schemas, representations, and interfaces

Clockwork makes a distinction that most schema languages do not: schemas as a concept are distinct from representations of the schemas and interfaces to those representations.
Briefly speaking, _schemas_ are an abstract data model, and each schema can have multiple concrete realizations as bits and bytes.
The realizations as bits and bytes are _representations_.
_Interfaces_ are the APIs used to access data in representations, so that users do not need to directly understand how the bits and bytes are laid out.

Schemas, representations, and interfaces are all specified using the [Clockwork domain-specific language (DSL)](../../dsl/README.md).
More in-depth information on these concepts is available in the [concepts document](../concepts/schema_repr_iface.md) on this subject.

This document focuses on the mechanics of schema definitions.
The mechanics of customizing, declaring, and instantiating representations and interfaces is covered separately [here](repr_iface.md).

## Schema syntax overview

The Clockwork DSL has a consistent syntax between schemas and other Clockwork entities (such as Cogs), but there is also some schema-specific syntax.
Here is a complete example schema showing several features:

```clockwork
// Hello, world!
schema HelloWorld
{
  uuid: cbe6ee0b-ec41-40ce-8587-fb3583e5385d;

  parameters
  {
    // Element of the `data` field.
    #2 data_type: Type = Byte;

    // Maximum number of elements in the `data` field.
    #3 data_max_size = 1024;
  }

  fields
  {
    // Sequence number
    #1 seqno: Int64 = -1;

    // Some data
    #4 data: VarArray<data_type, data_max_size>;

    // A self-referential identifier
    #5 msg_id: Uuid<HelloWorld>;

    // A timestamp
    #6 tov: SyncTime;

    // A duration with an init value
    #7 latency: Duration = 3.14ms;
  }
}
```

### A comment about comments

Note that syntactically, the "comments" are _required_ elements here, and leaving them out results in a syntax error from the compiler.
In the Clockwork DSL, there aren't comments in the sense that other languages have comments.
In Clockwork, there is only [documentation](common_syntax.md#documentation), and documentation is required for some elements and optional for others.
For schemas, parameters, and fields, documentation is required.

It's also not possible to "comment out" code in Clockwork, even temporarily, because it will probably result in a syntax error.
There aren't comments, only documentation, and documentation can only appear in places where the grammar expects it, associated with some specific other element of the grammar.

### Field and parameter numbers

You'll note that every field and parameter has a number assigned using syntax `#x`.
Even when attached to parameters, we typically refer to these just as "field numbers".
Field numbers serve two purposes, both related to providing backward compatibility when schemas evolve:

- Provide a stable identifier for the field or parameter, so it can be renamed with backward compatibility.
- Associate fields from prior versions to support upgrading previous versions to the current version.

If a schema doesn't have a history section the schema version is implicitly defined by the highest field number.
In the above example, the schema version number is 7, the highest field number.
When you add a new field to a schema that does not have a history section you also create a new schema version implicitly.
New fields must have a field number greater than the previous highest field number, so that they form a fully ordered sequence where the fields are ordered by when they were added to the schema.

A few rules:

- Never change the number of a field or enum value, because this will break backward compatibility.
- Gaps in the field number sequence are OK.
- Fields (and parameters) can be out of order in the schema source code; order of declaration does not matter.
  Feel free to reorder fields and parameters in the source code to group them logically for readability.
  The important part is that the field numbers record the history of the schema correctly.

#### Backward compatibility

Schema definitions may change over time while maintaining backward compatibility.
See [here](schema_history.md) for what changes are allowed and how to encode those changes in the schema definition.

### Schema UUIDs

Also to support schema evolution with backward compatibility, specifically the ability to rename or move schemas, we need a stable identifier for the schema.
That's the purpose of the `uuid` option in the example above.
Syntactically, the `uuid` is optional, so when drafting a schema you can leave it out.
When the Clockwork auto-formatter runs, if the UUID is missing, it will generate a new random UUID for you and insert it in the right place.

Every schema needs its own distinct UUID; never copy/paste the UUID from one schema to another.
The Clockwork compiler will detect duplicate UUIDs, but only at a much later stage of compilation, during system assembly.

### Order of elements

The order of the top-level schema block elements is fixed.
Trying to put `fields` before `parameters`, or `uuid` after `fields`, or any other rearrangement of those top-level constructs will result in a syntax error from the compiler.
This is done intentionally to enhance readability by enforcing consistency.

However, within these blocks, order is typically irrelevant.
Fields and parameters can be rearranged within their respective blocks in any order with no affect on the semantic meaning of the declarations.
Semantic ordering is derived from field numbers, not from declaration order.

## Clockwork types

Fields and parameters both have types, which can be either built-in or user-defined types.
User-defined types include schemas, enums, and tags (explained below).

### Built-in types

Built-in types are in the table below.

| Type                                                            | C++                                                                               | Python                                                           | Notes                                                                                                 |
| --------------------------------------------------------------- | --------------------------------------------------------------------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| `Bool`                                                          | `bool`                                                                            | `bool`                                                           | Not convertible to integers; values are `true` or `false`                                             |
| `Byte`                                                          | `std::byte`                                                                       | `int` when alone, `bytes` when in a container                    |                                                                                                       |
| `Float32`, `Float64`                                            | `float`, `double`                                                                 | `float`                                                          |                                                                                                       |
| `Int8`, `Int16`, `Int32`, `Int64`                               | `int8_t`, `int16_t`, `int32_t`, `int64_t`                                         | `int`                                                            |                                                                                                       |
| `UInt8`, `UInt16`, `UInt32`, `UInt64`                           | `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`                                     | `int`                                                            |                                                                                                       |
| `VarString<max_size: UInt64>`                                   | Custom bounded analog of `std::string`, typically accessed via `std::string_view` | `str`                                                            |                                                                                                       |
| `Bitset<size: UInt64>`                                          | `jewels::tap::Bitset<size>`                                                       | `int`                                                            | Fixed number of anonymous bits; `size` must be greater than zero.                                     |
| `FixedArray<type: Type, size: UInt64>`                          | `std::array<type, size>` but accessed via span                                    | `list[type]`                                                     |                                                                                                       |
| `VarArray<type: Type, max_size: UInt64>`                        | Similar interface to a `std::vector` but a custom Clockwork implementation        | `list[type]`                                                     | In C++ prefer to use spans, ranges, and iterators and avoid hard-coding the container type.           |
| `FixedSoa<type: Type, size: UInt64>`                            | `clockwork::FixedSoa<type, size>`                                                 | Dataclass with list fields                                       | Struct-of-Arrays layout. Requires `soa_enabled: true` on the element schema. See [SoA docs](soa.md).  |
| `VarSoa<type: Type, max_size: UInt64>`                          | `clockwork::VarSoa<type, max_size>`                                               | Dataclass with list fields                                       | Variable-size SoA layout. Requires `soa_enabled: true` on the element schema. See [SoA docs](soa.md). |
| `Tensor<type: Type, shape: List, layout: List \| TensorLayout>` | `jewels::tap::Tensor<type, shape, layout>`                                        | Dataclass with list fields for the elements, shape, and strides. | See section about tensors below for more details.                                                     |
| `Duration`                                                      | `std::chrono::nanoseconds`                                                        | Clockwork-specific `Duration` type                               | This is a strong type in Clockwork with [unit literal syntax](common_syntax.md#unit-literals).        |
| `SyncTime`                                                      | `jewels::time::SyncTime`                                                          | Clockwork-specific `SyncTime` type                               | This is also a strong type but without any literal syntax.                                            |
| `Uuid<tag: Type>`                                               | `jewels::Uuid<tag>`                                                               | `uuid.UUID`                                                      | Tag type is discarded in Python. See below for defining tag types in Clockwork.                       |

#### Bitsets

`Bitset<size>` represents a fixed number of anonymous, index-addressed bits. The size is a compile-time `UInt64` value greater than zero:

```clockwork
// Availability of configured sensors.
#0 sensor_present: Bitset<37>;
```

Its Tachyon representation occupies `ceil(size / 8)` bytes with one-byte alignment.
Bit `i` is byte `i / 8`, mask `1 << (i % 8)`.
Bit zero is the least-significant bit of byte zero.
The unused high bits of a non-byte-aligned final byte are written as zero.
Readers ignore those unused bits so malformed historical data does not change the logical value.

In C++, a field is `jewels::tap::Bitset<size>`.
It provides `test`, `try_set`, `try_reset`, `try_flip`, `reset`, `flip`, `all`, `any`, `none`, `count`, equality, and a read-only `bytes()` span.
Indexing outside `[0, size)` is invalid; use the callsig-safe `test(Out<bool>, index)`, `try_set`, `try_reset`, and `try_flip` when an untrusted index must be handled without throwing.
The throwing `test`, `set`, `reset(index)`, and `flip(index)` overloads are only for STL algorithm compatibility.

Generated and dynamic Python Tachyon APIs expose a bitset as a nonnegative `int`, where bit `i` is `1 << i`.
Serialization rejects negative values and values greater than or equal to `2**size`.
Bitsets have no nonzero schema init-value syntax; their default value is zero.

#### Tensors

The `Tensor` builtin is a convenient way to represent multi-dimensional data.
Its parameters are slightly richer than those of the other builtins.
The sizes of each of a tensor's dimensions are specified with the `shape` parameter, which must be a list literal whose elements can all bind to `UInt64` (constants and integer are both acceptable).
For example, setting `shape` to `[2, 3]` will yield a two-dimensional tensor where the first dimension has two elements and the second has three.
The memory layout can be specified with the builtin `TensorLayout` enum (see the table below).
You can also explicitly set the stride for each dimension by providing a list literal, like you would for the `shape` parameter.
If no layout is specified, `TensorLayout::row_major` is used.

| Value                        | Description                                                                             |
| ---------------------------- | --------------------------------------------------------------------------------------- |
| `TensorLayout::row_major`    | Stride[i] is the product of Dimensions[j] for all j in (i, n). Stride[n-1] is always 1. |
| `TensorLayout::column_major` | Stride[i] is the product of Dimensions[j] for all j in (0, i). Stride[0] is always 1.   |

### Clockwork strong types (Duration, SyncTime, UUID, tags)

#### Duration

The `Duration` type in Clockwork records a quantity of time, i.e., the difference between two time points.
Durations in Clockwork always have nanosecond resolution and are represented in C++ as `std::chrono::nanoseconds`.

Please note, you **must not** include a unit suffix (such as `_ns`) in the field name when using `Duration` type.
When converting to C++, Clockwork generates already a strong type `std::chrono::nanoseconds` so that unit confusion is not possible.
When creating representations (such as Protobuf) without strong type support, Clockwork will add the `_ns` unit suffix to the field name automatically.

Duration literals can be expressed with strong types using Clockwork [unit literal syntax](common_syntax.md#unit-literals), with units `ns`, `ms`, or `s`.
So for example: `3.14ms` would be 3.14 milliseconds, with the unit unambiguously attached, and the data type of that literal is `Duration`.
Since `Duration` is also used to specify execution frequencies for periodic Cogs, they can also be expressed with units `Hz`, which are converted to seconds and used as the period.

#### SyncTime

Time points also get a strong type in Clockwork: `SyncTime`.
This becomes a `jewels::time::SyncTime`.
Again, you **must not** include a `_ns` suffix on field names when using the strong type, which implicitly includes units.

There is no `SteadyTime` type in Clockwork, because there's no use case for including this type in a message.

#### UUID (and tagged types)

Clockwork has a `Uuid` strong type, which maps to `jewels::Uuid` in C++.
But here things get slightly more complicated, because like the C++ version, UUIDs in Clockwork are strong types with type tags, so that you can't confuse one kind of UUID for another.
This means we need a way to define type tags.

```clockwork
// Sample tag
tag Sample;
```

This defines a `Sample` tag.
Documentation is required for tags.

With that, you can define a specific UUID type as `Uuid<Sample>`, which would be a _good_ identifier.

It's not always necessary to define tags though, because any time you define a schema (or a Cog), the schema (or Cog) type is also directly usable as a tag type.
So if we have a `Track` schema defined already, and we want instances of this schema to have unique IDs, that can just be `Uuid<Track>` without any explicit `tag` declaration.
And because of Clockwork's multi-pass compiler, you can reference the name `Track` as a tag type even before you've finished defining the `Track` schema, without any forward declaration:

```clockwork
// An actor track
schema Track
{
  fields
  {
    // Track ID
    #1 id: Uuid<Track>;
  }
}
```

That's a perfectly valid (and encouraged) pattern; there is no reason here to define a separate `TrackTag` type.

Tags can be used with other types, not only `Uuid`.
Clockwork will be adding additional built-in strong types, but users can also make use of tag types when defining their own schema types.
Just include a [parameter](#parameters) with type `Type` in your schema, and you have a tagged schema type.

## Fields

The `fields` block is required and every schema must define at least one field.

### Field declaration syntax

```clockwork
    // Sequence number
    #1 seqno: Int64 = -1;
```

The components of a field declaration are:

- [Documentation](common_syntax.md#documentation) (required)
- [Field number](#field-and-parameter-numbers) (required)
- Field name/identifier (required)
- Field type expression (required)
- Field init value expression (optional)
- Semicolon (required)

### Field names

Field names must be unique across both fields and parameters, just like field numbers.
Renaming is supported for fields, but not parameters, with the schema history section.
Within each version of the schema, field names for that version are unique, but they may conflict across different versions as things are added, removed, and renamed.

Consider avoiding names which are keywords in the supported target languages C++ and particularly Python, though this is not essential as when we generate interfaces, the field names will usually be modified to avoid conflicts with language keywords.

For example, in C++ interfaces generally are of the form `get_foo` and `set_foo`, which prevents keyword conflicts.

In Python however, the primary interface is dataclasses with the field names as direct fields of the class; in this case the compiler will need to modify the field name by appending an underscore if it conflicts with a keyword, which will impact readability.

So concretely, it's probably a terrible idea to name a field `if`.
(And not only because of keyword conflicts.)
A more common example would be a field named `id`, which is not a language keyword in Python but it is a built-in function, and though this does not cause a syntactic conflict, syntax highlighters will usually display that in a different and misleading color.

### Init values

In this example, we show the optional init value syntax `= -1`.
Please note that init values are not the same thing as "defaults" in other schema languages, and using them has some performance and correctness considerations.
All Clockwork fields are initialized; Clockwork never gives uninitialized data to user code, so the C++ problems of undefined behavior resulting from uninitialized fields does not exist, whether an init value is specified here or not.
If there is no init value specified in the schema, the data is zero-initialized.

Do not use init values unless they are truly needed, and consider how the use of init values interacts with the overall software design and particularly how it affects safety.
There is a separate [guidance document](../guidance/init_values.md) that provides a detailed description of init values and guidance for their use.

### Expression syntax

Above, both the field type and the init values are described as "expressions".
This has a specific and relevant meaning.
Field types and init values can indeed be expressions, which are evaluated by the DSL compiler to produce the final type/value.[^2]

[^2]:
    Actually, types are also first-class values in Clockwork, so an expression that produces a type is actually producing a value with type `Type`.
    Expressions always produce values.

This will most often be relevant when using [generic parameters](#parameters) to make schemas customizable.
For example, consider this other field from the example schema:

```clockwork
    // Some data
    #4 data: VarArray<data_type, data_max_size>;
```

Here we have a complex type expression which references two generic parameters, `data_type` and `data_max_size`, used as arguments to an expression that instantiates the generic type `VarArray`.
Init values, being expressions, can also reference generic parameters, to make the init value configurable.

Expressions are type-checked by the compiler, and any type mismatch will result in a compilation error.

## Parameters

The `parameters` block is optional, but many schemas will have parameters.
Parameters are generic formal parameters; here, "generic" is used in the sense of generic programming, also known as template programming in C++ terminology.
Parameters are therefore _compile time_ constructs and a schema with parameters (which is therefore a _generic schema_) must be instantiated with arguments known at compile time, just like C++ template instantiations.
There can of course be multiple instantiations of a generic schema, just like C++ can have multiple instantiations of template classes.

Syntactically, parameter declarations are nearly identical to field declarations (see below for differences).
Because parameters can be added, removed, and modified over time, they also must have field numbers, so that schemas can be properly versioned with backward compatibility.
Parameters and fields share the same name space for their names and the same "number space" for their field numbers, so there cannot be duplicates in field names or numbers between parameters and fields.
(This is enforced automatically by the compiler.)

### Parameter defaults (vs field init values)

Syntactically, parameters can have what looks just like an init value for a field.
But despite the similar syntax, these are not init values; these are defaults, which semantically are a bit different from init values.
Parameter defaults, for example, don't have the same concerns for runtime performance or code correctness/safety as init values do.
Parameter defaults are static and handled at compile time, becoming part of the concrete type when the schema is instantiated.

Parameter defaults, like init values, are expressions and are type-checked.

### Explicit and inferred parameter types

The one syntactic difference between parameters and fields is that while an explicit type is _required_ for fields, it is in many cases optional for parameters.
Parameter types can be inferred by the compiler in most cases, but field types must always be explicit—even when the compiler _could_ infer them, it will refuse to.

This is seen in the example schema:

```clockwork
    // Element of the `data` field.
    #2 data_type: Type = Byte;

    // Maximum number of elements in the `data` field.
    #3 data_max_size = 1024;
```

Here the `data_type` parameter has an explicit type `Type`, meaning it expects a type value.
In this case, we provide the default value `Byte`, which is in fact a type so this checks out.

But for `data_max_size` we have no explicit type.
The type is inferred by the compiler based primarily on the context in which the parameter is _used_.
It's used later in the schema as the argument for the `max_size` parameter of `VarArray`, which has type `UInt64`, so it's inferred to have type `UInt64`.

Whether inferred or explicit, the compiler ensures that all types always agree.
So the default value `1024` will be type-checked to ensure it's compatible with `UInt64`.
Any problems with type inference or type checking are compile-time errors.

We have [guidance on when to use inferred vs explicit types](../guidance/type_inference.md).
Please read and follow this guidance.

## Schema composition

Clockwork schemas can include other schemas as fields.
The only restriction is that schemas cannot recursively include each other, directly or transitively.

To create a field using another schema, you must first make the sub-schema definition visible to the super-schema.
This can be done by defining both of them in the same Clockwork source file, or by importing the sub-schema's Clockwork module in the super-schema's module.

Let's look first at a simple example with both schemas defined in one module:

```clockwork
// Top-level schema
schema SuperSchema
{
  fields
  {
    // A contained schema
    #1 sub_object: SubSchema<max_things=30>;
  }
}

// Contained schema
schema SubSchema
{
  parameters
  {
    // Max number of things
    #1 max_things = 10;
  }

  fields
  {
    // The things
    #2 things: VarArray<UInt32, max_things>;
  }
}
```

In this example, an instance of `SubSchema` is contained within `SuperSchema`.
Note that declaration order is flexible; `SuperSchema` is able to refer to the name `SubSchema` before it's defined, even without forward declaration.
Clockwork does not need forward declarations because it uses multi-pass compilation and only performs name resolution after the initial pass over the syntax.
So feel free to define things in the natural order, which in many cases is to start with the top-level schema as shown above.

This also shows an example of instantiating a schema with parameters.
`SubSchema` has a size parameter, which is provided by `SuperSchema` when it instantiates `SubSchema`.
In `SuperSchema`, we could also just use the default value for this parameter by instantiating the type as `SubSchema<>`.
Or, we could percolate the parameter further up the stack, by giving `SuperSchema` a parameter and passing this parameter down into the `SubSchema` instantiation.

This works also with enum definitions, which can be used as schema field types.
Enums can also be defined in the same file as a schema that uses them, in any order.

But in many cases, you'll want to put schemas in different modules.
In that case, you just import what you need.
Details on how to import other Clockwork modules are provided [in this separate document](attributes.md).

## Defining enums

Enums use a very similar syntax to schemas:

```clockwork
// Enum
enum HelloEnum
{
  uuid: bea931ce-fba6-4abd-941c-16ab44088aec;
  values
  {
    // Normal greeting
    #1 hi default;

    // A bit more formal
    #2 hello;

    // Spanish
    #3 hola;

    // Mandarin
    #4 ni_hao;

    // Hindi
    #5 namaste;
  }
}
```

Enums do not (currently) support generic parameters, and instead of a `fields` block there is a `values` block.
Value declarations have a similar syntax to field declarations, but do not support a type specification nor an init value.
They have value numbers, which follow the same rules as schema field numbers, and serve the same purpose: to allow enums to evolve over time with backward compatibility.

At the schema level, you can think of Clockwork enums as abstract, strong types, which have nothing to do with integers.
They are not convertible to integers in the Clockwork DSL, even with explicit casting.

Of course, they do become normal C++ and Python enums when no longer in abstract Clockwork DSL space.
And as such, they do have underlying integer representations.
By default the integer values are determined by Clockwork, and might change with different versions of the enum.

In particular, the value number in the Clockwork source **is not** the underlying integer value.
Value numbers identify values so that values can be added, removed, and renamed with backward compatibility.
Because of the ability to evolve enums in this way, the underlying integer value for each enum value can change in different versions of the enum.

To declare a schema with an enum field, just follow the same process as for [schema composition](#schema-composition).

### Enum default values

There is also one piece of syntax specific to enum values: the `default` keyword.
Every enum must designate one (and only one) of its values as `default`.
The default value does not need to be the first value listed.

The purpose of `default` is to determine which value a zero-initialized enum value will take on.
This has both correctness and performance considerations, which are discussed in greater depth in the [init values guidance document](../guidance/init_values.md).
Please read and understand that guidance when deciding which enum value to designate as default.

Generally your enum should either be designed to be invalid by default, forcing users to set it to a valid value explicitly (this is [approach 2 in the guidance](../guidance/init_values.md#approach-2-invalid-sentinel-value-with-sanity-checking)), or it should have a default value which is generally valid and _semantically correct_ ([approach 1 in the guidance](../guidance/init_values.md#approach-1-valid-values-by-default)).
If there is no semantically correct default value, you should have an explicit `invalid` or `uninitialized` member of the enum which is the default.

Note that once we implement enum evolution features, not only will they support adding, removing, and renaming values, but also it will be possible to change which value is `default` in different enum versions.
This will not break backward compatibility per se, but it will change the semantics of any schema which uses the enum and leaves enum values at the default value.
From the point of this change on, that will be a different default than it was before.
Old data can still be loaded and will have the correct values, but the semantics and interpretation of the data may be changed due to the change in default.

### Enum representation and underlying values

While Clockwork will determine the appropriate underlying representations by default, it is possible to manually specify both the underlying integer type of the enum, and an underlying value for each enum value.
This can be useful if your use case requires a specific set of underlying values, like representing some externally defined set of enumeration values.
It also allows for stability in enum definitions as a schema changes.

```clockwork
// Enum with underlying types and values.
enum UnderlyingEnum
{
 options
 {
   underlying_type: UInt16;
 }

 #0 new_val default { underlying_value: 0; }
 #1 val1 { underlying_value: 1; }
 #2 val2 { underlying_value: 2; }
 #4 val3 { underlying_value: 3; }
}
```

In the above example, the type of the enum is specified in the `options` block as the `underlying_type`.
Each enum field has a block where the `underlying_value` can be specified.

### Enum values as bit flags

In some use cases, it is useful to define enum values as independent bit flags instead of incrementing integral values.
Adding `bit_flags;` to the `options` section of the enum declaration tells Clockwork to generate underlying values for each enum field that are a single bit (i.e. 1, 2, 4, 8...).
In the generated code the enum values can then be used as orthogonal bit masks.
