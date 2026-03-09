# Reduced boilerplate migration

This file provides guidance for migrating from the original Clockwork file format to the new reduced boilerplate Clockwork file format.
Readers should be familiar with the [attributes and code generation](attributes.md) and [schema representations and interfaces](repr_iface.md) documents.

## General considerations

- If a module that generates a language (cpp, py, nanobind, proto, protoconv) then the modules it imports must generate that language with a few exceptions:
  -- If a module only generates cpp for strong type definitions then that module can be imported into modules that generate other languages by specifying `[cpp]` on the use statement.
  -- If a module doesn't generate anything for a language, then it can be imported by omitting the language from the use statement.
- Non-low boilerplate Clockwork modules (no `generate` attribute) can only be imported with a `use[]` statement.
- Multiple targets per Clockwork source file are not allowed, each Clockwork source file generates a single output for each language.

## Migrating schema and enum definitions


The first step in migrating any file to low-boilerplate is to add a `generate` attribute at the top of the Clockwork file, right after any file comments.
The `generate` attribute tells the compiler what code to generate for the entities defined in the source file.
The `generate` attribute also tells Gazelle what needs to built when the Clockwork file is compiled.
Then any language specific attributes need to be set immediately following the `generate` attribute.
Most files just to set the C++ namespace and use the defaults for everything else.

Use statements might need to be annotated to limit which languages in imports from modules that don't generate code for some languages.
The big one is `jewels::units::au` which can be imported for any language but only generates C++ code for strong types.

```clk
use[cpp] jewels::units::au;
```

Any parameterized schemas need to be explicitly instantiated with an `instantiate` statement.
Best practice is to use an alias for any instantiations that use non-default parameters, and to omit the alias when instantiating a schema with default parameters.
For example:

```clk
schema NameExample
{
  parameters
  {
    #1  name_length: UInt32 = 128;
  }
  fields
  {
    #2 name: VarString<max_size=name_length>;
  }
}

instantiate NameExample<>;
instantiate LongNameExample: NameExample<name_length=1000000>;
```

Any C++ code that was using interface aliases defined in a `cpp_target` statement needs to be modified to use `Tappy` since Clockwork no longer generates the aliases in C++.
The C++ to reference the instantiated types in the above example would be `Tappy<NameExample<>>` and `Tappy<LongNameExample>`.

Then delete any targets declared in the file, we don't need those anymore.
Finally delete any of the rules to compile the Clockwork file or any of the code it generates and run Gazelle.

## Migrating C++ cog definitions

The first step in migrating a Clockwork source file that defines cogs is to add a `generate` attribute to tell the compiler you want to generate `cpp` and `cpp_cog`.

```clk
#![generate(cpp, cpp_cog)]
#![cpp(namespace=YOUR::NAMESPACE::HERE)]
```

Next modify any use statements to add language targets if needed as described above.
The implementation for the cogs defined in a Clockwork file needs to be `<filename>_clk_cc_impl.cc` if they aren't already and the build rule to compile the implementation needs to be updated.

Once that is done you can delete the `cpp_target` from the Clockwork file.
Then delete the build rules for the Clockwork file and any of the code it generates and run Gazelle.

Clockwork files that define multiple protobuf enums require some extra handling.
By default, Clockwork uses the value name from the Clockwork source file in the generated `proto` file.
This can cause problems because protobuf enum value names need to be unique within a package.
To work around this problem you can either define each enum in a separate Clockwork source files with unique package names, or you can set `prefix_enum_value_names` to true in the proto attribute.
If `prefix_enum_value_names` is `true` then Clockwork will create an upper snake case name from the enum name and the value name.
The generated protobuf value name for `on` in the following example would be `SWITCH_POSITION_ON` instead of `on`.

```clk
#!generate(proto)

#[proto(prefix_enum_value_names=true)]
Enum SwitchPosition
{
values
{
// Unspecified
#0 unspecified default;

    // On
    #1 on;

    // Off
    #2 off;

}
}

```

## Migrating C++ executables

C++ executables are often, but not always, defined in the same source file as the cogs that run in them.
That doesn't need to change for low-boilerplate.
Either way you need to add `cpp_exe` to the generate attribute for the Clockwork file.

To generate the executable in the same file as the cog definitions:

```clk
#![generate(cpp, cpp_cog, cpp_exe)]
```

To generate the executable in a separate file:

```clk
#![generate(cpp_exe)]
```

Then update the use statements appropriately and delete the `cpp_executable` statement from the Clockwork file.

Note that the name of the C++ executable will be `<filename>_clk_exe` which is probably not what you had before, so any references to the old name will have to be updated.

Finally delete the build rule for the Clockwork file and anything it generates and run Gazelle.

## Migrating python cogs and executables

The steps for migrating python cogs are about the same as for C++, except that the generate targets need to change to `py_cog` and `py_exe`.

## Migrating system targets

Low boilerplate Clockwork does not support the `system_target` statement, so any files that generate systems need to be split up, with the `system_target` system in a separate file from everything else.
