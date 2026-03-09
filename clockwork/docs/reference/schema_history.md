# Clockwork schema evolution DSL syntax

_Schema evolution_ provides a mechanism to change schema definitions over time while keeping backward compatibility with previous schema versions.
This document describes the allowed changes to schemas and enums in Clockwork, and the syntax used to specify these changes.

## Core versioning concepts

### Logs store the schema definitions for their contents

All versioning capabilities derive from the ability to use the logged channel metadata stored in each log to generate an upgrader that upgrades the logged messages to the current version.
Fields can be added, deleted, renamed or modified so long as the schema version number is increased with each change.
Deleted fields must also be recorded in the schema history to prevent the field number from being reused.

The schema history is recorded in the `history` section of the schema definition syntax.
Here is a brief example.

This is the initial definition of the example schema.
This schema does not have a history section, so the schema version number is implicitly defined to be the maximim field number contained in the schema, in this case the version is 1.

```clockwork
// Initial schema definition (version is 1)
schema SomeSchema
{
 fields
 {
   #0 boolean: Bool = true;
   #1 obsolete: Int32;
 }
}
```

Next we add another field to the example schema without any extra boilerplate.
We skipped field number 2 for some reason and that is allowed as long as the field number is greater than any other field numberss already in the schema.
The version is still implicit, adding the new field increases the version from 1 to 3.

```clockwork
// After adding first field (version is 3)
schema SomeSchema
{
 fields
 {
   #0 boolean: Bool = true;
   #1 obsolete: Int32;
   #3 integer: Int64;
 }
}
```

Renaming and modifying a field in the example schema requires adding a history section so we can increase the schema version number.
The history section makes the version number explicit, so the version is no longer calculated from the field numbers.

```clockwork
// Renaming integer field to helpful_name and changing type to Int32 (version is 4)
schema SomeSchema
{
 fields
 {
   #0 boolean: Bool = true;
   #1 obsolete: Int32;
   #3 helpful_name: Int32;
 }
 history
 {
    version: 4;
 }
}
```

Deleted fields need to be recorded in the history section to ensure that the field number is never reused.
The version number must increase with every change.

```clockwork
// Removed field 1 (version is 5)
schema SomeSchema
{
 fields
 {
   #0 boolean: Bool = true;
   #3 helpful_name: Int32;
 }
 history
 {
    version: 5;
    removed: [1];
 }
}
```

### Logical version numbers and physical version hashes

> [!TIP]
> tl;dr: Every field (and parameter) in the schema has a number.
> If the schema does not have a history section then the version number is implicitly defined as the maximum field number
> in the schema.

Every schema has a monotonically increasing logical version number.
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

## The `version` field

The `version` specification within the `history` block contains the logical version number of the schema.
If the schema does not have a history block, the version number is implicitly defined as the maximum field number in the schema.
The `version` must be increased every time the schema is changed.

## The `deleted` field

The `deleted` specification within the `history` block contains the field numbers that have been deleted from the schema.

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

Changing parameters does not require increasing the schema version because schemas instantiated with different parameters are different types.

### Remove fields

Fields can be removed as long as the history is updated to show they've been removed.
Here we have a schema where field 0 has been renmoved.
Removing the field also increased the version to 2.

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
   version: 2;
   removed: [0];
 }
}
```

Once a field is removed, its name can be reused in a new field, but the old field number must not be reused.
(The compiler will prohibit reuse of numbers listed in the deleted field.)

There is no relationship between fields with the same name in different versions.
If a field is removed and then a new field with the same name is added, the new field is not considered to be a continuation of the old field.

### Rename fields

Fields can be renamed as long as the schema version is also increased.
The previous name can be reused for a different field in any version on or after the rename version.
In the example below, we have a single-field schema where the field was renamed:

```clockwork
schema SomeSchema
{
 fields
 {
   #0 new_name: Bool;
 }
 history
 {
   version: 1;
 }
}
```

### Init values

Init values can be changed (including added or removed), but changing init values is a version change.

Below we have a boolean field which started without an init value (implicitly it was `false`) and changed to `true` in version 1.

```clockwork
schema SomeSchema
{
 fields
 {
   #0 some_field: Bool = true;
 }
 history
 {
   version: 1;
 }
}
```

### Change field type

The data type of a field may be changed so long as there is a conversion from the old type to the new type.

The syntax for this also requires increasing the sequence number.
The syntax is the same as for any other field change, such as rename or init value, shown above.

#### Integer type changes

Any integer type can be changed into any other integer type, including a smaller type, or signed to unsigned or vice versa.
However, this may cause a runtime error during upgrade if the new type cannot represent the old value.

Integer types may also be converted to SyncTime and Duration, but not vice versa.

#### Floating point type changes

`Float32` and `Float64` are convertible to each other, but not to integer types.

#### Container type changes

Containers include `VarArray`, `FixedArray`, `VarSoa`, `FixedSoa`, `Optional`, and `VarString`.
With the appropriate history annotation, the container type can change between any of these, and the size specification can also change.
In this context `Optional` is treated as equivalent to a `VarArray` with max size of one; it either contains one element or none.
`VarString` is treated as having an element type of `Byte` and is assumed to be UTF-8 encoded.

This can cause a runtime error if the new container type's size cannot hold the existing data.
In particular, if changing from `VarArray` to `Optional`, the array must have 0 or 1 element.
More than one element will cause a runtime error.
Changing the size of `FixedArray` is not allowed, so changing the type from `Optional` or `VarArray` to `FixedArray` has limited usefullness since the upgrade will fail at runtime unless the size of the `FixedArray` is correct for all instances in the log.

#### Array-of-Structs to Struct-of-Arrays conversions

Fields can be converted between Array-of-Structs containers (`VarArray`, `FixedArray`) and Struct-of-Arrays containers (`VarSoa`, `FixedSoa`) of the same element type.
The upgrade system automatically transposes data between layouts.

For example, `VarArray<Point3f, max_size=100>` can be changed to `VarSoa<Point3f, max_size=100>`, and vice versa.
Similarly, `FixedArray<Point3f, size=10>` can be changed to `FixedSoa<Point3f, size=10>`.

Conversions between fixed and variable-size containers (e.g., `FixedSoa` to `VarSoa`) follow the same rules as other container type changes.

See the [SoA containers reference](soa.md) for more information on Struct-of-Arrays containers.

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
Renaming a schema does not require increasing the version number.

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
Just like with schemas, values can be removed as long as the history is updated to show they've been removed.
The syntax is the same as for schemas.
If a value is `removed` and it is present in old data, that data will fail to upgrade (runtime error when attempting to upgrade the log).

<!-- vale off -->

Our guidance therefore is to use `removed` for an enum value only if you are sure that that value was never actually used or at least never logged in any log that we care about.

<!-- vale on -->

### Rename values

Existing values may be renamed.
When renaming a field the value number must not change and the enum version must be incremented.
The syntax is the same as for schemas.

> [!WARNING]
> Renaming enum values may cause the underlying values to change!
> The Clockwork log upgrade process will handle the mapping from the old to new values if necessary.

### Change bit_flags option

An enum may be changed from non-bit-flags to bit-flags, but this is a version change.
When changing from bit-flags to non-bit-flags, attempting to upgrade data with multiple bits set will result in a runtime error.

See next section for an example.

### Change underlying type and underlying values

An enum without underlying type and values specified may add these, or existing specifications may be removed.

Existing underlying values may also be changed.

This example shows a complete example where the `bit_flags` option is being removed, the underlying type is changing, and the underlying value of one value is changing.
The schema before the change was this:

```clockwork
// Before  the change
enum SomeEnum
{
 options
 {
   bit_flags;
   underlying_type: UInt8;
 }

 #0 new_val default { underlying_value: 0; }
 #1 val1 { underlying_value: 1; }
 #2 val2 { underlying_value: 2; }
 #3 val3 { underlying_value: 3; }
```

After the change the schema became:

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
 #3 val3 { underlying_value: 4; }

 history
 {
   version: 4;
 }
}
```

The above means that in version 3, the enum was a bit flag enum with underlying type `UInt8`.
In version 4, the current options (non-bit-flags, `UInt16`) are in effect.
There was also a change in the underlying value of `val3` from 4 to 3.

> [!NOTE]
> In this example, because there is a change from bit-flags to non-bit-flags, there may be runtime errors when upgrading logs that contain multiple bits set.

### Change default value

The value selected as a default may be changed.
This would require increasing the version number, but we only need to increase it by one.

## Enum changes which do not change version

The `linter_override` option may be freely changed and this does not change the logical or physical version.
Documentation may also be changed.

## Disallowed enum changes

The enum UUID may not be changed.
Value numbers may not be changed.

## Schema Backward Compatability Testing

Clockwork provides a unit test script that can be used to check that the logged channels for a system can be upgraded from a previous version of the schemas for those channels.
The following Bazel macro is from the [demo system](../../examples/demo_system/README.md) and provides an example of how to validate that changes to the logged channel schemas can be upgraded from a version of the metadata stored in the repo.
Typically one would use external storage to upload the metadata for each version of the schema and then use this unit test to check compatability before allowing a new version of the schema to merge into the repo.

```bazel
sh_test(
    name = "validate_logged_channel_metadata",
    srcs = ["@clockwork//clockwork/serialization/cpp:validate_logged_channel_metadata_test.sh"],
    args = [
        "$(location {0})".format("//clockwork/serialization/cpp:validate_logged_channel_metadata"),
        "$(location {0})".format("//clockwork/examples/demo_system/resources:logged_channel_metadata.bin"),
        "$(location {0})".format(":demo_system.demo_system_sys.logged_channel_metadata.pbbin"),
    ],
    data = [
        ":demo_system.demo_system_sys.logged_channel_metadata.pbbin",
        "//clockwork/examples/demo_system/resources:logged_channel_metadata.bin",
        "//clockwork/serialization/cpp:validate_logged_channel_metadata",
    ],
)
```

There are some channels that get logged but don't need backward compatibility.
Use the _enforce_backwards_compatibility_ option in the channel definition to disable the upgradability test for that channel.
The upgradability test on channels is enabled by default and is disabled by setting _enforce_backwards_compatibility_ to false.

```clk
// GPS channel
channel GpsChannel
{
  name: "/gps";
  message_type: Tachyon<gps_message::GpsMessage>;
  max_num_messages: 10;
  enforce_backwards_compatibility: false;
}
```
