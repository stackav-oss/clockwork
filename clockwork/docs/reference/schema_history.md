# Clockwork schema evolution DSL syntax

_Schema evolution_ provides a mechanism to change schema definitions over time while keeping backward compatibility with previous schema versions.
This document describes the allowed changes to schemas and enums in Clockwork, and the syntax used to specify these changes.

## Core versioning concepts

### The source code records a complete history of the schema

All versioning capabilities derive from the same basic architecture, which is that the schema definition in the Clockwork domain-specific language (DSL) captures the entire history of the schema.
Fields can be marked removed in the schema, meaning they're no longer present in the current version, but they cannot be actually deleted from the DSL source code.
This allows intelligent transformation of data from older schemas into the current version, tracking data type changes, field renames, and more.
The core idea here is that information must never be lost from the source code–at least not without breaking backward compatibility.

This is accomplished with the `history` section of the schema definition syntax.
Here is a brief example showing several history features simultaneously; below these features are explained in detail:

```clockwork
schema SomeSchema
{
 fields
 {
   #6 boolean: Bool = true;
   #4 integer: Int64;
   #7 floating_point: Float64;
 }
 history
 {
   versions: [2, 3, 7];
   version_pseudofields: [3]
   fields
   {
     #2 obsolete: Int32 -> removed #3;
     #0 fp: Float32 -> became #5;
     #1 boolean: Bool -> became #6;
     #5 floating_point: Float32 -> became #7;
   }

   schema
   {
     name
     {
       #1 OldSchema;
     }
   }
 }
}
```

### Logical version numbers and physical version hashes

> [!TIP]
> tl;dr: Every field (and parameter) in the schema has a number.
> Every one of these numbers **is also a version number**.
> When you add a field or parameter, that creates a new version number.
> Some of these versions are intermediate and are never actually instantiated.

Every schema has a monotonically increasing logical version number, which is equal to the highest field number in the schema definition.
This mechanism relies on the schema definition being maintained properly as described above.

The logical version number identifies a particular version of a schema _non-recursively_.
This is a critical point to understand; schemas can contain other schemas directly or transitively.
**When any sub-schema changes, that creates a _physical_ change in the schema(s) which contain it.**
Therefore, every schema has a physical version hash which changes when the logical version number changes, or when any transitively-contained schema changes.

Note that the physical hash can change without any changes to the schema definition DSL source file.
This is because it can reference schemas defined in imported source files.
But the logical version number cannot change without a change to the schema definition.

Both the logical version number and physical version hash can be introspected as needed to support different use cases.
Unit tests can also be written that enforce for example no transitive changes (without updating the unit test).

## The `versions` field

The `versions` specification within the `history` block contains the logical version numbers which are supported by the schema.
In this case, “supported” means any version you want to be able to read and convert to the current version; this should generally be any version that was ever released or which was used to record logs which still need to be read.

Versions can be removed from this list if it is certain that no old data of that version still exists or needs to be available for conversion to the latest version.

It is not required or recommended for every logical version to be present in this list.
Many logical versions do not exist in any logs; for example, if in a single change two different fields are added, this creates two new logical versions, but only one of them will ever appear in a log.
Only that one should be in this list.

**The current schema version must always be in the versions list.**
If no `history` block is present, the implicit specification is that only the current version is supported.

### Version pseudofields

> [!TIP]
> See the [Remove fields](#remove-fields) section to understand why you'd use this feature.

Logical version numbers and field/parameter numbers are directly connected in Clockwork; a new logical version is created any time a field or parameter is created; the version number is the field number.

Sometimes it’s needed to create a new version without creating a new field or parameter, such as when removing a field without adding another one.
To support this, we have `version_pseudofields`.
Adding a field number to this list creates a new logical version and also reserves that number so that it cannot be used for fields or parameters.

## Allowed schema changes that change the version

### Field changes vs parameter changes

In the current implementation, **no parameter changes** are allowed **at the schema level**.
This means that the formal parameters (in the `parameters` section) may not be added, removed, or changed in any way.

However, a schema which contains a sub-schema may change the parameters of that sub-schema, subject to limitations.
Parameters can be used in different ways within the schema, and some ways that parameters are used could result in incompatible changes when the parameter is changed.
The core rule is that if the instantiated schema with the new parameter value(s) is field-wise compatible with the instantiated schema with the old parameter value(s), then the change is allowed.
Here "instantiated" means that the parameter value has been fully substituted into the schema definition.

#### Examples of Allowed Parameter Changes

By far the most common type of parameter change is changing the size of a container.
If the parameter is used to define the size of a container, then changing the parameter value is allowed.
But that is not the only case where parameters can be changed.
Consider this schema:

```clockwork
schema ContainerAndFeature
{
 parameters
 {
   #0 value_type: Type;
   #1 max_size: UInt64;
   #2 feature_type: Type;
 }
 fields
 {
   #3 container: VarArray<value_type, max_size>;
   #4 feature: feature_type;
 }
}
```

Let's say you have a schema which includes a field of this type, with instantiation `ContainerAndFeature<value_type=UInt32, max_size=10, feature_type=SomeEnum>`.
You would be allowed to change `max_size` to any other size, because (after substitution) this results in only a container size change, which is allowed.
You'd also be allowed to change `value_type` to `Int32`, because integer types are compatible with each other (see below).

However, you would not be allowed to change `feature_type` to `SomeOtherEnum`, because enum types are not compatible with each other (at this time).
Nor could you change `value_type` to `Float64` (because integer-to-float conversions are not currently allowed), nor could `value_type` change to something completely different like `String`.

### Remove fields

Fields can be marked removed, but can’t be actually deleted from the source code.
Here we show a field `old_field` that was removed in version 2.
Version 2 is itself a pseudofield version (no new field was added at that time).

```clockwork
schema SomeSchema
{
 fields
 {
   // Bool
   #1 boolean: Bool = true;
 }
 history
 {
   versions: [1, 2];
   version_pseudofields: [2];
   fields
   {
     #0 old_field: Int32 -> removed #2;
   }
 }
}
```

Completed details of the old field are included in the `fields` block, so that no information is lost.
Once a field is removed, its name can be reused in a new field, but the old field number must not be changed or reused.
(The compiler will prohibit reuse of numbers.)

There is no relationship between fields with the same name in different versions.
If a field is removed and then a new field with the same name is added, the new field is not considered to be a continuation of the old field.

### Rename fields

Fields can be renamed but both the old and the new names must be retained in the schema definition.
A field rename is like any other change and must be recorded in a new version.
The previous name can be reused for a different field in any version on or after the rename version.
In the example below, we have a single-field schema where the field was renamed:

```clockwork
schema SomeSchema
{
 fields
 {
   #1 new_name: Bool;
 }
 history
 {
   versions: [0, 1];
   fields
   {
     #0 old_name: Bool -> became #1;
   }
 }
}
```

### Init values

Init values can be changed (including added or removed), but changing init values is a version change.

Below we have a boolean field which started without an init value (implicitly it was `false`) and changed to `true` in version 1.
Note that the syntax for init value changes is identical to the syntax for renaming; both just use `became`.
In fact, `became` is the field change multi-tool; it can make any of the supported field changes, one at a time or all at once.

```clockwork
schema SomeSchema
{
 fields
 {
   #1 some_field: Bool = true;
 }
 history
 {
   versions: [0, 1];
   version_pseudofields: [2];
   fields
   {
     #0 some_field: Bool -> became #1;
   }
 }
}
```

### Change field type

The data type of a field may be changed so long as there is a conversion from the old type to the new type.

The syntax for this also uses `became`, so we don’t really need to show it.
The syntax is the same as for any other field change, such as rename or init value, shown above.
You just copy the previous field definition to the `history` block, change the type, and use `became` to point to the new version of the field.

#### Integer type changes

Any integer type can be changed into any other integer type, including a smaller type, or signed to unsigned or vice versa.
However, this may cause a runtime error during upgrade if the new type cannot represent the old value.

Integer types may also be converted to SyncTime and Duration, but not vice versa.

#### Floating point type changes

`Float32` and `Float64` are convertible to each other, but not to integer types.

#### Container type changes

Containers include `VarArray`, `FixedArray`, `Optional`, and `VarString`.
With the appropriate history annotation, the container type can change between any of these, and the size specification can also change.
In this context `Optional` is treated as equivalent to a `VarArray` with max size of one; it either contains one element or none.
`VarString` is treated as having an element type of `Byte` and is assumed to be UTF-8 encoded.

This can cause a runtime error if the new container type's size cannot hold the existing data.
In particular, if changing from `VarArray` to `Optional`, the array must have 0 or 1 element.
More than one element will cause a runtime error.

#### Non-container to container type changes

It is also allowed to change from a non-container type to a container type, so long as the container element type and the previous non-container type are convertible.
For example: `Int32` to `Optional<Int64>` or `VarArray<UInt32, max_size=N>` is allowed.
(In the latter case, it becomes a single-element array.)
It is not possible to change from a container type to a non-container type.

### Add new fields

New fields can be added, which implicitly creates a new version.
When upgrading data from an older schema version, new fields will be given their init values.

Adding a field is simply adding it to the `fields` section of the schema as usual, and then updating the `versions` field of the `history` block to include it.
If the `history` block is not yet present, it needs to be created, and `versions` should contain the previous version and the new version.

### Rename schema

The schema can be renamed, but the schema UUID must not be changed.

```clockwork
schema NewName
{
 fields
 {
   // Bool
   #1 boolean: Bool = true;
 }
 history
 {
   versions: [1, 2];
   version_pseudofields: [2];
   schema
   {
     name
     {
       #1 OldName;
     }
   }
 }
}
```

This means that in version 1 (and earlier) the name was `OldName`, and in versions after that it is `NewName`.

## Allowed schema changes that do not change the version

The constructor options may be changed without changing the logical or physical versions.
Documentation may also be changed freely.

## Disallowed schema changes

Schema UUID may not be changed.

Field numbers may not be changed.

Parameter definitions (including default values) currently may not be changed.

## Enum upgrade handling

In general, enum underlying types and values can change when enum definitions change.
Therefore enums are also upgraded during schema upgrade, just like schemas.
This provides quite a lot of freedom; a lot of existing enum best practices are based on maintaining backward compatibility through careful assignment of stable underlying values, but Clockwork enums do not have this requirement due to the upgrade process.

However, when Clockwork enums are designed to correspond to some external value specification (e.g., in matching Protobuf schemas or other external systems), then they will have manually-specified underlying values and types.
In this case, maintenance of backward compatibility with the external systems must also be done manually.

> [!IMPORTANT]
> Enum values have value numbers (e.g. `#0`, `#1`, etc.) which are used to identify them in the schema definition.
> These numbers must not be changed.
> These numbers are _entirely unrelated_ to the underlying integer values of the enum.
> During enum evolution, even if underlying values were correlated with enum numbers, the changes may break this relationship.

## Allowed enum changes

Enums also have a logical version number and a physical version hash, but they always change in lockstep, since enums cannot have sub-enums or sub-schemas.
Every change made to an enum is a version change and every change might change the underlying representation data type and the underlying values, unless underlying values are specified in the enum definition.

### Add new values

New values may be added without restriction.
The `versions` list in the `history` block should be updated just as with schemas.

> [!WARNING]
> Adding new values to an enum may cause the underlying type to change!
> If nothing relies on underlying type (a good practice) this is fine.
> The Clockwork schema upgrade take care of old data.

### Remove values

> [!WARNING]
> Removing enum values may cause underlying values of other values to change!
> It may also change the underlying type.
> If nothing relies on underlying values/type (a good practice) this is fine.
> The Clockwork schema upgrade process will renumber values and adapt the type.

Values may be removed.
They must remain in the enum definition, just like for schemas, but they’re no longer available in C++ or Python.
When values are removed from an enum with automatic underlying type and values, values will be renumbered and the underlying type might shrink if this is possible.

There are two ways to remove values: `removed` or `became`.
If a value is `removed` and it is present in old data, that data will fail to upgrade (runtime error when attempting to upgrade the log).
If instead `became` is used, then the old value is replaced with the new value on upgrade.

<!-- vale off -->

Our guidance therefore is to use `removed` for an enum value only if you are sure that that value was never actually used or at least never logged in any log that we care about.

<!-- vale on -->

The syntax is the same as for schemas.

### Rename values

Existing values may be renamed.
The old names remain in the enum definition but are no longer accessible in C++ or Python.
The syntax is the same as for schemas.

> [!WARNING]
> Renaming enum values may cause the underlying values to change!
> The Clockwork schema upgrade process will renumber values if necessary.

### Change bit_flags option

An enum may be changed from non-bit-flags to bit-flags, but this is a version change.
When changing from bit-flags to non-bit-flags, attempting to upgrade data with multiple bits set will result in a runtime error.

See next section for an example.

### Change underlying type and underlying values

An enum without underlying type and values specified may add these, or existing specifications may be removed.
In both cases, all fields must either have underlying values specified or none may have values specified, _in any version present in the versions list_.
(Removed fields must not be changed however.)

Existing underlying values may also be changed.

This shows a complete example where the `bit_flags` option is being removed, the underlying type is changing, and the underlying value of one value is changing:

```clockwork
enum SomeEnum
{
 options
 {
   underlying_type: UInt16;
 }

 #0 new_val default { underlying_value: 0; }
 #1 val1 { underlying_value: 1; }
 #2 val2 { underlying_value: 2; }
 #4 val3 { underlying_value: 3; }

 history
 {
   versions: [3, 4];
   values
   {
     #3 val3 { underlying_value: 4; } -> became #4;
   }
   enum
   {
     options
     {
       #3
       {
         bit_flags;
         underlying_type: UInt8;
       }
     }
   }
 }
}
```

The above means that in version 3 and earlier, the enum was bit flags with underlying type `UInt8`.
After version 3, the current options (non-bit-flags, `UInt16`) are in effect.
There was also a change in the underlying value of `val3` from 4 to 3.

> [!NOTE]
> In this example, because there is a change from bit-flags to non-bit-flags, there may be runtime errors when upgrading logs that contain multiple bits set.

### Change default value

The value selected as a default may be changed.
This would require changing both fields with `became`, but only one version would be added to the `versions` list.

## Enum changes which do not change version

The `linter_override` option may be freely changed and this does not change the logical or physical version.
Documentation may also be changed.

## Disallowed enum changes

The enum UUID may not be changed.
Value numbers may not be changed.
