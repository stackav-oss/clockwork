# Clockwork schema representations and interfaces

This document covers defining and customizing schema representations and interfaces.
For a thorough discussion of the _concepts_ of schemas, representations, and interfaces, see the [Concepts document](../concepts/schema_repr_iface.md) on this subject.
This document is a reference guide for the nitty-gritty of representations and interfaces.
Schema definition is documented separately [here](schemas.md).

## Customizing representations

In many cases, a default representation of each representation type for a schema is available without any need to specify it.
But sometimes customization is required, and there's a syntax for that.

At the moment, only one representation type supports customization: Tachyon, the Clockwork native binary format.
The only customization it currently supports is alignment specifications.
Alignment can be very important for Tachyon data because it's used directly, without separate serialization/deserialization, on both CPUs and GPUs.
When using SIMD instructions on CPUs, or doing just about anything on GPUs, data alignment is critical for performance, or sometimes just to make the instructions work at all.

By default, Tachyon aligns each data type by its natural alignment, which is equal to its size in bytes.
But this can be customized with the following syntax.

```clockwork
schema HelloWorld
{
  fields
  {
    #1 foo: Int64;

    #2 data: VarArray<Int32, max_size=1024>;
  }
}

representation Tachyon<HelloWorld>
{
  options
  {
    align: 512bit;
  }

  fields
  {
    #2 data
    {
      align: 16byte;
    }
  }
}
```

Here, we are customizing the default representation for the `HelloWorld` schema to add alignment constraints.
Specifically, in the `options` section, we specify the alignment of the schema representation itself, so that the first byte of the Tachyon representation of `HelloWorld` will be on a 512-bit boundary.
In the `fields` section we can further align each field of the Tachyon representation, and here we ensure that the `data` array begins on a 16-byte boundary.

> [!IMPORTANT]
> These specifications _only_ affect the Tachyon representation.
> Other representations are unaffected.

Note also the use of unit literals with `bit` and `byte` units.
For alignments, you can use whichever is most natural, which is usually whatever unit the hardware specifies its alignment in.
Internally, Clockwork converts this all to bytes and ensures that the alignment makes sense: that is it a power of two and also a multiple of the natural alignment of the data type.

### Default vs named representations

Above we are changing the _default_ Tachyon representation of `HelloWorld`.
When you change the default representation options, that **must** be done **in the same file** as the schema is defined.
This ensures that all users of the schema see the same default representation.
It would be a serious problem if this were defined in separate modules, because different parts of the code could then disagree on the alignment of fields in the default Tachyon representation of the schema.

However, it's also possible to create separate named representations, with different options.
This might be important because, for example, you might want one alignment specification on one hardware platform, but a different one on another.
Because we have a strict separation between the concept of schema and representation, we can have multiple representations of the same schema.

For each _type_ of representation (such as Tachyon), there can be only one _default_ set of representation options for a given schema.
But there can be as many named Tachyon specializations as you like.
Here's an example:

```clockwork
representation Pachyon: Tachyon<HelloWorld>
{
}
```

Here we create a representation with the name `Pachyon`.
This is no longer a default representation specification, so it does not need to be in the same module as `HelloWorld`.
Anything that needs this representation will need to separately import it if it's not, but there's no chance of confusion because anybody wanting this representation will need to explicitly select it by name.

But the specification above is empty, so what does it mean?
It means we are explicitly requesting the representation defaults, meaning the real defaults, not the overridden defaults we defined above.
So the `Pachyon` named representation has natural alignment for everything, and will be as compact as possible.
Or in other words, it will be packed, hence the name.

So the only point of the above specification would be to explicitly undo the default alignment we specified above.
Different schema users could then explicitly opt in to the packed version of the schema by requesting `Pachyon` by name.

## Instantiating representations

Nothing we've discussed so far, here or in the [schemas](schemas.md) documentation, has actually resulted in any generated code.

Schemas are entirely abstract and have no code associated with them directly.
We only generate code for representations and interfaces.
But just customizing a representation, as above, also doesn't result in code generation, it only affects what code we'll ultimately generate.

Code is generated only on request for specific combinations of representation, interface, and programming language.

Below we show how to instantiate representations and interfaces in C++, which results in C++ code generation.

### `cpp_target` blocks

```clockwork
cpp_target hello_world
{
  options
  {
    namespace clockwork::demo;
  }
}
```

This defines an empty C++ code generation target `hello_world`.
(We'll make it non-empty soon.)
The name given here is the basis of file names and also the Bazel target.
So this will create files `hello_world.hh`, `hello_world.cc`, `hello_world.inl`, and possibly `hello_world_fwd.hh` (for forward declarations), and put that all in a `cc_library` Bazel target called `hello_world`.

We specify the namespace for the entities we're about to instantiate in this target with the `namespace` option.
Clockwork does not itself have namespaces other than the module names, but when we translate to C++ we must specify a namespace.
Everything in a `cpp_target` will be in the same namespace.

### Instantiating entities in a C++ target

The above target is empty, which isn't very useful.
Let's instantiate a few things:

```clockwork
cpp_target hello_world
{
  options
  {
    namespace clockwork::demo;
  }

  schema HelloWorld;
  tag SampleTag;

  representation Tachyon<HelloWorld>;
  interface Tap<Tachyon<HelloWorld>>;
}
```

Note that documentation is optional for all instantiations, but could be provided.
Above we instantiate the schema `HelloWorld`.
This actually only creates a tag type for the schema, but every schema needs to have this tag instantiated somewhere (and only in one `cpp_target`).
Similarly we instantiate the tag `SampleTag`, which again is only a tag type.

Then we instantiate the _default_ Tachyon representation for `HelloWorld`.
At this point we are generating serious code.
This will cause a bunch of C++ code implementing the Tachyon wire format layout in C++, which is a prerequisite for being able to access that data.

Note that Tachyon can only be instantiated for a complete, non-generic schema.
Because `HelloWorld` (the example from the [schemas reference](schemas.md)) has generic parameters, we must instantiate it.
Here, we instantiate it with default parameters, but we could also specify specific arguments here.

We need an interface in order to do anything useful with a representation, and so we instantiate a Tap interface for that representation.
Tap interfaces, in order to provide high performance, must be instantiated for specific representations.

### Adding aliases

Instantiated representations and interfaces can also be given aliases, which can make them less verbose to refer to within C++ code.
This is most useful for the interfaces, because generally C++ code does not need to refer to representations by name.

```clockwork
  representation Tachyon<HelloWorld>;
  interface HelloWorldTap: Tap<Tachyon<HelloWorld>>;
```

This does exactly the same thing as the previous instantiation, but additionally creates a type alias `HelloWorldTap`, which will reduce wear and tear on the kbd:[\<] kbd:[>] keys on your keyboard.

### Deciding where to instantiate

Consider dependency management when deciding what `cpp_target`s to create, and what to instantiate in which target.
Generally speaking, err on the side of more targets rather than fewer, so that users can depend on only what they need.

In the example above, if `HelloWorld` were used in multiple representations, it would make sense to separate the `schema` instantiation into its own `cpp_target`, and each representation plus interface combination into its own `cpp_target`.

However, all of these `cpp_target` definitions can be together in the same Clockwork module, because simply parsing a Clockwork file with the target definition does not automatically create a dependency on the C++ code in that target.
Putting the `cpp_target` definitions close to the definitions of the things they instantiate (typically in the same Clockwork module) is a good practice, because it does not significantly increase compile time of the things depending on that Clockwork module.
However, there is no restriction on this; a `cpp_target` can instantiate any Clockwork entity whose definition it imports.

### `Tappy`

An alias `Tappy<...>` is provided that is short hand for `Tap<Tachyon<...>>`.
The `Tappy` alias can be used both within the clockwork language to refer to a Tap-Tachyon interface, but also within C++ to refer to a Tap-Tachyon class type.

### Instantiating entities in a Python target

Entities can be instantiated in a `py_target` similar to how its done for `cpp_target`s.
Below is an example:

```clockwork
py_target clkpy_hello_world
{
  enum SomeEnum;

  representation Tachyon<HelloWorld>;
  interface Tap<Tachyon<HelloWorld>>;
}
```

These produce a Python module with instantiations of dataclasses that represent schemas.
See `py_tap_test.py` in `clockwork/dsl/serialization/tests/` for examples of how to use the generated dataclasses.
In `clockwork/dsl/tests/support/`, the `pytapmsg` and `msg_with_au_py` targets in `tapmsg.clk` and `msg_with_au.clk` respectively are referenced in the linked test.

For non-generic schemas, the name of the dataclass will be the same as the schema.

To instantiate an interface for a generic schema in Python, you must provide an alias and that alias will be the name of the dataclass.

### `proto_target` blocks

Another supported representation is `Protobuf`.
This representation generates a protocol buffer schema based on the contents of the Clockwork schemas being represented.
A `Protobuf` representation can only be declared in a `proto_target` block.
Because this only results in a Protobuf schema being generated, `Protobuf` is considered to only be a representation and not an interface.

Because an underlying protocol buffer message must be generated for each schema being represented, all generic schemas must have an alias.
The alias provided will then be the name of the generated protocol buffer message.
Note that tags are not used in protocol buffer representations, and therefore all UUID types are interchangeable.
See the table below for a full list of conversions between Clockwork types and protocol buffer types.

#### Clockwork to protocol buffer type mappings

| Clockwork                                                                         | Protobuf      |
| --------------------------------------------------------------------------------- | ------------- |
| Bool                                                                              | bool          |
| Byte, VarArray\<Byte, maxSize>                                                    | string        |
| Float32, Float64                                                                  | float, double |
| Int8, Int16, Int32                                                                | sint32        |
| Int64                                                                             | sint64        |
| UInt8, UInt16, UInt32                                                             | uint32        |
| UInt64                                                                            | uint64        |
| VarString\<max_size: UInt64>                                                      | string        |
| FixedArray\<type: Type, max_size: UInt64>,VarArray\<type: Type, max_size: UInt64> | repeated      |
| Duration                                                                          | Duration      |
| SyncTime                                                                          | Timestamp     |
| UUID                                                                              | String        |

A .proto file will be generated for each `proto_target` block.
The file name will be the name given to the `proto_target` block with a .proto extension.

Due to the nature of how protocol buffers treat arrays and optionals, VarArrays of VarArrays and Optional VarArrays are not supported by Protobuf representations.
However, one could have a VarArray of schemas that contain VarArrays, similarly an Optional of a schema that contains a VarArray is supported.

In the example below, note how an alias is specified for `Protobuf<ContainsAVarArray<Float32, 2056>>`.
The alias is what tells the schema generator what to call the protocol buffer message that represents an instantiation of a `ContainsAVarArray<Float32, 2056>`.

```clockwork
schema HelloWorldNoProtobuf
{
  fields
  {
    #1 foo: Int64;

    // This can't be used in protocol buffer representations!
    #2 data: VarArray<VarArray<Int32, max_size=1024>, 2056>;
  }
}

schema ContainsAVarArray
{
  parameters
  {
    // How many
    #1 size: UInt64;

    // Of what
    #2 data_type: Type;
  }

  // What is being wrapped
  #3 my_data: VarArray<data_type, size>;
}

schema HelloWorldProtoCompatible
{
  fields
  {
    // This is fine to use  in protocol buffer representations
    #1 nested_data: ContainsAVarArray<Float32, 2056>;
  }
}

proto_target hello_world
{
  options
  {
    package clockwork.demo;
  }

  representation my_container: Protobuf<ContainsAVarArray<Float32, 2056>>;
  representation Protobuf<HelloWorldProtoCompatible>;
}
```

### Converters

In some use cases, it may be desirable to convert between the different supported representations at runtime.
Clockwork provides the ability to specify a `converter`.
Declaring a converter inside of a `cpp_target` block results in conversion functions being generated in C++.
Currently the conversions supported are `ProtobufToTap` and `TapToProtobuf`.
In the example below, a `ProtobufToTap` converter is declared for the `HelloWorld` schema.
The first argument to the converter is the input representation, which is a Protobuf.
Because Protobuf is only a representation and not an interface, the raw representation is specified.
However, the second argument is the Tap interface.
This specifies that the Tap interface to `HelloWorld` should be used for setting the output.
The `Tappy` alias is also supported as are any aliases defined for the representations and interfaces being converted.

In some cases, one may want to validate that all fields in a proto object have been set prior to converting it to Tachyon.
The `proto_target` has a `validate_proto` option that is defaulted to false.
However, when set to true, all Protobuf representations defined in that target, will fail conversion if a field or, even a field in a contained representation, is not explicitly set prior to the conversion.
Regardless of whether this field is set, a `validate_protobuf` function will be generated.

The converter in this case generates a function called `protobuf_to_tap`.
The `protobuf_to_tap` function takes a reference to the Tap output to be populated as its first argument, and the input protobuf object as its second argument.

The `protobuf_to_tap` C++ conversion function returns a `jewels::expected` object which can be used to determine whether the conversion was successful, and if it wasn't successful, it will contain the error message.

The converter is placed in files corresponding to the name of the `cpp_target` block it is contained in.
So the converter in the example below will be placed in `hello_world_converter.hh`, `hello_world_converter.cc` and `hello_world_converter.inl`.

`TapToProtobuf` mostly behaves in the same way as `ProtobufToTap` except that it expects a Tap interface for the first argument and a protobuf representation for the second.
This converter generates a function called `tap_to_protobuf`.
The `tap_to_protobuf` function takes a reference to the protobuf output to be populated as its first argument, and the input Tap object as its second argument.
Unlike `protobuf_to_tap` this conversion cannot fail, and therefore returns void.

It is important to note that for nested schemas, all contained schemas must also have a converter explicitly defined.

```clockwork
cpp_target hello_world
{
  options
  {
    namespace clockwork::demo;
  }

  schema HelloWorld;
  tag SampleTag;

  representation Tachyon<HelloWorld<>>;
  interface Tap<Tachyon<HelloWorld<>>>;
  converter ProtobufToTap<Protobuf<HelloWorld>, Tap<Tachyon<HelloWorld>>>;
  converter TapToProtobuf<Tappy<HelloWorld>, Protobuf<HelloWorld>>;
}

proto_target hello_world_proto
{
  options
  {
    package clockwork.demo_proto;
  }

  representation Protobuf<HelloWorld<>>;
}
```

### Example BUILD file

Below is the Bazel BUILD file you would need to build all of the libraries for the generated code in the example above.

```bazel
clk(
    # Assuming the name of our clockwork file is hello_msg.clk
    name = "hello_msg_clk",
    srcs = ["hello_msg.clk"],
    outs = [
        hello_world.cc,
        hello_world.hh,
        hello_world.inl,
        hello_world_converter.inl
        hello_world_converter.cc,
        hello_world_converter.hh,
        hello_world_proto.proto,
    ],
)

proto_library(
    name = "hello_world_proto",
    srcs = [
        "hello_world_proto.proto,
    ],
    visibility = ["//visibility:public"],
)

proto_compile(
    name = "hello_world_proto_compile",
    outputs = [
        "hello_world_proto.pb.cc",
        "hello_world_proto.pb.h",
    ],
    plugins = [
        "@build_stack_rules_proto//plugin/builtin:cpp",
        "//tools/gazelle:protoc-gen-nolint",
    ],
    proto = "hello_world_proto",
    visibility = ["//visibility:public"],
)

proto_cc_library(
    name = "hello_world_proto_cc_library",
    srcs = ["hello_world_proto.pb.cc"],
    hdrs = ["hello_world_proto.pb.h"],
    visibility = ["//visibility:public"],
    deps = ["@protobuf"],
)

cc_library(
    name = "hello_world",
    srcs = [
        "hello_world.cc",
        "hello_world.inl",
    ],
    hdrs =["hello_world.hh"],
    visibility = ["//visibility:public"],
    deps = [
        ":hello_world_proto_cc_library",
        "@clockwork//jewels/container/tap:protobuf_to_tap",
    ],
)
```
