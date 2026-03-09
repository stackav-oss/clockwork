# Clockwork schema representations and interfaces

This document covers defining and customizing schema representations and interfaces.
For a thorough discussion of the _concepts_ of schemas, representations, and interfaces, see the [Concepts document](../concepts/schema_repr_iface.md) on this subject.
This document is a reference guide for the nitty-gritty of representations and interfaces.
Schema definition is documented separately [here](schemas.md).

> [!NOTE]
> This tutorial uses Clockwork file format that is recommended for new development.
> The original Clockwork language format is somewhat more verbose but it is also still fully supported.
> See the [original reference](legacy_repr_iface.md) to learn about the original language format.

## Customizing representations

Customizing representations is not supported in the new Clockwork file format.
This feature is supported in the original format but has never been used.

## Instantiating schemas

Nothing we've discussed so far, here or in the [schemas](schemas.md) documentation, has actually resulted in any generated code.

Schemas are entirely abstract and have no code associated with them directly.
We only generate code told to do so by a `generate` attribute in the source file.

### Generate cpp attribute

Say we have a Clockwork source file named `hello_world.clk` and we want to generate C++.
Adding these lines after the documentation in the source file tells Clockwork to generate C++ code in the `clockwork::demo` namespace.

```clockwork
#![generate(cpp)]
#![cpp(namespace(clockwork::demo)]
```

The generated source files will be named `hello_world_clk_cc.cc`, `hello_world_clk_cc.hh`, and `hello_world_clk.cc.inl` and will contain the C++ definitions of everything defined in the source file that translates to C++ code.

Enums, constants, and external types have the same names in C++ as they do in Clockwork and are defined in the specified namespace.
Non parameterized schemas also have the same name as they do in Clockwork, but need to be referenced using `Tappy<SchemaName>` (short for `Tap<Tachyon<SchemaName>>`).

Parameterized schemas must be instantiated in the Clockwork source file to tell the compiler to generate code for them.
By convention, schemas that have defaults for all parameters are instantiated like this:

```clockwork
instantiate SchemaName<>;
```

You can only have one instantiate statement for each schema without a specific alias.
The names of the aliases specified in the instantiate statement must be unique within the Clockwork source file and also within the specified C++ namespace.

The generated schema instantiation will not have an alias in C++ and is referenced in the code as `Tappy<SchemaName<>>`.
The name for the generated schema for other languages is just `SchemaName`.

Instantiations can also be used to generate code for various parameters by providing an alias.
For example, the following would instantiate the schema for two other parameter values.

```clockwork
instantiate SchemaNameSmall: SchemaName<size=1>;
instantiate SchemaNameLarge: SchemaName<size=1000000>;
```

These instantiations are referenced in C++ as `Tappy<SchemaNameSmall>` and `Tappy<SchemaNameLarge>`.
The names for other generated languages are simply SchemaNameSmall and SchemaNameLarge.

### Instantiating entities in python

Entities can be instantiated in python simply by adding "py" to the generate attribute in the Clockwork source file.
Below is an example:

```clockwork
#![generate(py)]
```

The generated source file will be named `hello_world_clk_py.py` and will contain the python definitions of everything defined in the source file that translates to C++ code.

See `py_clk_tap_test.py` in `clockwork/dsl/serialization/tests/` for examples of how to use the generated dataclasses.
In `clockwork/dsl/tests/support/`, the `clk_tapmsg_clk_py` target generated from `clk_tapmsg.clk` is referenced in the linked test.

### Instantiating entities in Protobuf

Another supported representation is `Protobuf`.
This representation generates a protocol buffer schema based on the contents of the Clockwork schemas being represented.
Entities can be instantiated in Protobuf simply by adding "proto" to the generate attribute in the Clockwork source file.
Because this only results in a Protobuf schema being generated, `Protobuf` is considered to only be a representation and not an interface.
Below is an example:

```clockwork
#![generate(proto)]
#![proto(package=clockwork.demo.proto)]
```

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

A .proto file will be generated for each Clockwork source file with a `proto` in the `generate` attribute.
The file name will be the Clockwork source file stem appended with `_clk_proto.proto` extension.

Due to the nature of how protocol buffers treat arrays and optionals, VarArrays of VarArrays and Optional VarArrays are not supported by Protobuf representations.
However, one could have a VarArray of schemas that contain VarArrays, similarly an Optional of a schema that contains a VarArray is supported.

In the example below, note how an alias is specified for the instantiate statement for `Protobuf<ContainsAVarArray<Float32, 2056>>`.
The alias is what tells the schema generator what to call the protocol buffer message that represents an instantiation of a `ContainsAVarArray<Float32, 2056>`.
The alias defaults to the schema name if it no alias is specified.

```clockwork
#![generate(proto)]
#![proto(package=clockwork.demo.proto)]

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

instantiate my_container: ContainsAVarArray<Float32, 2056>;
instantiate HelloWorldProtoCompatible;
```

### Converters

In some use cases, it may be desirable to convert between the different supported representations at runtime.
Clockwork provides the ability to specify a `converter`.
Specifying a "proto_conv" target in the `generate` attribute tells the compiler to generate conversion functions in C++;
Generating "proto_conv" requires that the file also generate "proto" and "cpp".
Currently the conversions supported are `ProtobufToTap` and `TapToProtobuf`.
Setting these attributes in the Clockwork source file results in Protobuf and C++ code generation for the schemas plus code to convert the schemas defined in the from Protobuf to Tap.

```clockwork
#![generate(cpp, proto, proto_conv)]
#![cpp(namespace=clockwork::demo)]
#![proto(package=clockwork.demo.proto)]
#![proto_conv(tap_to_protobuf=false)]
```

In some cases, one may want to validate that all fields in a proto object have been set prior to converting it to Tachyon.
The `proto` attribute has a `validate_proto` option that is defaulted to `true`.
When set to `true`, all Protobuf representations defined in that target will fail conversion if any field (including sub-fields in contained types) is not explicitly set prior to the conversion.
Regardless of whether `validate_proto` is `true` or `false`, a `validate_protobuf` function will always be generated.

The converter in this case generates a function called `protobuf_to_tap`.
The `protobuf_to_tap` function takes a reference to the Tap output to be populated as its first argument, and the input protobuf object as its second argument.

The `protobuf_to_tap` C++ conversion function returns a `jewels::expected` object which can be used to determine whether the conversion was successful, and if it wasn't successful, it will contain the error message.

The converter is placed in files corresponding to the name of the Clockwork source file it is defined in.
So if the source file is named `hello_world.clk` then the converter will be placed in `hello_world_clk_proto_conv.hh`, `hello_world_clk_proto_conv.cc` and `hello_world_clk_proto_conv.inl`.

`TapToProtobuf` mostly behaves in the same way as `ProtobufToTap` except that it expects a Tap interface for the first argument and a protobuf representation for the second.
This converter generates a function called `tap_to_protobuf`.
The `tap_to_protobuf` function takes a reference to the protobuf output to be populated as its first argument, and the input Tap object as its second argument.
Unlike `protobuf_to_tap` this conversion cannot fail, and therefore returns void.

It is important to note that for nested schemas, all contained schemas must also have a converter explicitly defined.

### Example BUILD file

Below is the Bazel BUILD file you would need to build all of the libraries for the generated code in the example above.

```bazel
clk(
    # Assuming the name of our Clockwork file is hello_msg.clk
    name = "hello_msg_clk",
    srcs = ["hello_msg.clk"],
    outs = [
        hello_world_clk_cc.cc,
        hello_world_clk_cc.hh,
        hello_world_clk_cc.inl,
        hello_world_clk_proto_conv.cc,
        hello_world_clk_proto_conv.hh,
        hello_world_clk_proto_conv.inl
        hello_world_clk_proto.proto,
    ],
    generate = ["cpp", "proto", "proto_conv"]
)
```
