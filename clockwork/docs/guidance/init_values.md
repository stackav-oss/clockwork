# Guidance on init values in Clockwork schemas

Clockwork supports field init values (aka initial values), but using them has negative performance implications.
In this document we explain how init values work, how they affect performance, and when they should or should not be used.

## Init value introduction

You can specify init values when defining fields in a schema:

```clockwork
schema Foo
{
  fields
  {
    #1 foo: Int64 = -1;
  }
}
```

This defines a field `foo` with type `Int64` and init value -1.

### Init values vs parameter defaults

Do not confuse [field init values](../reference/schemas.md#init-values) with [parameter defaults](../reference/schemas.md#parameters):

```clockwork
schema FooBar
{
  parameters
  {
    #1 foo: Int64 = 5;
  }
  fields
  {
    #2 bar: Int64 = 6;
  }
}
```

Here there is a generic parameter `foo` and a field `bar`.
The parameter `foo` has a _default_ value of 5, and the field `bar` has an _init value_ of 6.
Parameter defaults are fundamentally different than init values and have no performance penalty because they are processed at compile time; feel free to use them whenever a default is appropriate for the parameter.
Init values for fields are the subject of this document.

### Meaning of init values

Init values provide the initial value for a field, when a schema instance is default-constructed.
This most often happens immediately prior to Cog execution, when Clockwork creates and initializes the messages that the Cog is about to publish.
Any field which does **not** have an init value will be zero-initialized, and fields which do have init values will be set to their init value.

### Init values and enums

Clockwork [enums](../reference/schemas.md#defining-enums) require a [default](../reference/schemas.md#enum-default-values) value to be specified when defining the enum.
This value will be represented by the integer value 0, which means that zero-initializing an enum field automatically gives you the default enum value.
You do **not** need to provide the default enum value as the init value of an enum field in order to get this behavior, and indeed you should not for performance.
Only provide an init value for enum fields if you need the enum to be initialized to something other than the default.

If you find yourself needing to do this, there's probably a problem with the enum design.
Consider changing the enum default instead—enums are versioned and their defaults can be changed without breaking backward-compatibility.
(Though this change will affect the default value in every schema where the enum is used, which is a global semantic change, but it does not break backward-compatibility.)
Generally your enum should either be designed to be invalid by default, forcing users to set it to a valid value explicitly (this is [approach 2](#approach-2-invalid-sentinel-value-with-sanity-checking) below), or it should have a default value which is generally valid and _semantically correct_ ([approach 1](#approach-1-valid-values-by-default) below).
If there is no semantically correct default value, you should have an explicit `invalid` or `uninitialized` member of the enum which is the default.

## Performance implications

As mentioned above, all Clockwork data _without_ init values is always zero-initialized.
There is never uninitialized data in Clockwork, in the C++ sense of uninitialized data that leads to undefined behavior.
Clockwork data is always initialized with well-defined values.

If every field in the data (recursively, if the schema contains other schemas) can be zero-initialized, then this is very fast.
As soon as any field in the schema (recursively) has an init value, the initialization will become slower, and the more complex the schema (recursively), the slower it will be.
This performance impact will be felt on every execution of any Cog that publishes that schema, when Clockwork initializes the output message with the init values.

This is why this document exists: To help developers get safe code behavior without unnecessary performance degradation.

### Implications for data in containers

When using a `FixedArray` container type, then every element of the array will be initialized, either zero-initialized or using init values, just like C++ `std::array`.
So you will pay the performance hit for init values for every element in the array.

When using a `VarArray` type, the situation is better; elements will be initialized only when they are added to the array, just like for `std::vector`.
If you add elements by calling `resize`, they will be default-constructed, and you pay the initialization performance cost.
But if you add elements by `emplace_back`, when they are constructed from the data you provide in the emplace call, which means default initialization is bypassed (unless you emplace a default).

So using `VarArray` in combination with `emplace_back`, once you know the data you want to emplace and can provide it in the emplace call, is one way to avoid all of the performance cost of init values for elements of the container, because it avoids default construction in the first place.

## Guidance

First, despite the warnings above about possible performance implications above, if using init values improves the safety or reliability of the system, then use init values.
That's what they're there for, and safety is more important than the relatively small (in most cases) performance impact of init values.
The purpose of this guidance is to avoid poor usage of init values which do not improve safety.
Good usage of init values is strongly tied to overall software design, as explained below, and bad usage of init values may be a sign of poor design.

So really, think of this guidance as being less about getting maximum performance, and more about thinking about what the use of init values says about the overall software design and how that relates to code safety and reliability.

### Avoid wasteful init value usage

If you are publishing data, it's because you're producing data.
If the data you produce always has the same value for a field (the init value), then you're not actually producing that data, and maybe it isn't needed at all.
You would use an init value, presumably, because there are some code paths in your Cog where you _do_ set that field, and some code paths where you do _not_ set that field.
And that really gets to the heart of the matter, which we talk about in more detail in the following sections.

But first, know that if the field has an init value, then every time you take a code path that sets the field, the time spent initializing the value to the init value was wasted.
This is, though, usually a very small performance hit in most cases.

But consider instead, especially if you set the field more often than not, just ensuring that every code path sets that field value, and then init values are not needed.
This is essentially [approach 2](#approach-2-invalid-sentinel-value-with-sanity-checking) below, and it has not just performance benefits but can also have safety and reliability benefits.

### Approach 1: Valid values by default

One good argument in favor of init values is safety: to ensure that the data is always valid in some way, or meets some invariant, even if some code paths do not set it.
If the code is designed so that, by design, some nominal code paths do not set the field, and that is considered correct behavior, then this can make sense.

Clearly, for best performance, it's ideal if the semantically correct default value for the field also happens to be zero.
Clockwork enums, for example, can be configured so that the appropriate default value has integer value zero, making zero-initialization both correct and efficient.
Init values are not necessary in this case.

For this approach to make sense, then the init value must be a semantically correct default value.
If there is no semantically correct default value, then using an init value simply to ensure that the data always _appears_ valid is, in fact, just masking an underlying bug.
If the "valid" value is not semantically correct, then it's not truly valid, and forcing it to meet some invariant by default does not improve safety.

### Approach 2: Invalid sentinel value with sanity checking

This line of reasoning leads to our next potential use case for init values.
If the code is instead designed so that all code paths _should_ set every field, then the init value could be used as an explicitly invalid sentinel value, flagging the field as uninitialized.
At the end of Cog execution (and/or in the data consumer), a data validator can then check to see that this sentinel value is no longer present.
If the sentinel value remains, then we know there's a bug in the producer; some code path is not setting that field to a valid value.

This is a very good idea for safety.
Ideally, you can use zero as the sentinel value, which even makes this fast.
(Though the cost of data validation later is likely far greater than the initialization cost, clearly data validation is worth the cost.)
So, yes use invalid sentinel values to represent "uninitialized", and try to use zero as the sentinel value if at all possible.
Init values are not needed if zero is the sentinel value.

Clockwork's enum mechanism is designed to make exactly this both easy and efficient.
If you designate the invalid sentinel value as the `default` for the enum, then it will be represented by integer 0, meaning zero-initialization already sets it to the sentinel value, with no extra performance cost.

If zero is a valid value, though, such as for numeric data, then by all means use an init value for this purpose.
It's not worth trying to hack your way to zero being an invalid value if it is naturally a valid value; any such workaround will probably have worse performance than just paying the cost of initialization.

### Deciding between approaches

These approaches are essentially opposites of each other.
In approach 1, following a code path that does _not_ set the value is a design choice and is nominally correct.
In approach 2, we make that incorrect by design instead, and use sentinel values to ensure the code is behaving as designed.

Which one is the best in any given situation really depends on the situation, though we can firmly say that if there is no reasonable, semantically-correct default value, then it is clearly incorrect to follow approach 1.
But if there is a semantically-correct default value for a field, then approach 1 is an option, though approach 2 is also always an option.

If both approaches are viable, consider whether zero makes more sense as a valid default or an invalid sentinel.
This need not be the deciding factor, but if all else is equal, this can help determine which approach to take.
