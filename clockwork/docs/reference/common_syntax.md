# Clockwork common syntax reference

This is a reference guide for common syntax elements in the Clockwork language, such as literals and documentation comments.
Syntax for specific elements (like schemas and Cogs) is covered in the reference guides for those elements.

## Documentation

Clockwork does not have "comments" in the sense that most programming languages use that term.
Clockwork has, instead, documentation elements, which look a lot like C++ `//` comments, but they are more restricted syntactically.
They cannot appear just anywhere, but only in specific locations in the syntax, where they are associated with specific other syntax elements.
Each documentation block provides documentation for the syntax element that immediately follows it.

Some elements, like schemas, parameters, and fields, have required documentation blocks.
Failing to provide documentation for them results in a syntax error.
Other elements have optional documentation.

It's also not possible to "comment out" code in Clockwork, even temporarily, because it will probably result in a syntax error.
Trying to "comment out" code using documentation syntax may or may not work, but it will always be wrong, because the commented block will be interpreted as documentation for the element which follows it.

### Documentation syntax and formatting

The only supported documentation syntax in Clockwork is `//`, which must be followed by a space or an immediate newline.
If you accidentally use `///` for example (as does Doxygen in C++), that will be a syntax error.

```clockwork
// This is a schema example.
//
// We like schemas.  They're very important for representing data.
//
// Schemas have fields.  This schema has one field called `data`, which stores
// a 64-bit unsigned integer.  It's not very interesting.
schema Example
{
  fields
  {
    // Example data
    #1 data: UInt64;
  }
}
```

This shows the basic documentation syntax, for a schema and its (one) field.
Documentation for fields can be one line, as shown here, or a block, as shown for the schema.
When providing more complex documentation, such as for the schema here, the first line is the "brief", which should be the best summary of the entity's purpose that you can fit in one line.
This should be followed by a "blank" line, meaning `//` with no following text.
(An actual blank line would terminate the documentation block and result in a syntax error.)

AsciiDoc formatting can be used in documentation comments, but keep it simple because complex AsciiDoc markup will likely make the documentation less readable in the source code.
Here we are using blank lines to delimit paragraphs, and some simple `monospace` inline formatting to refer to a field.
It's also reasonable to use list markup, `__emphasis__`, and `***bold***` as appropriate.
Links or xrefs to external documentation could also be appropriate.
Avoid anything more complex than that.

For normal AsciiDoc documents, we enforce one-sentence-per-line, but this need not be followed for Clockwork in-source documentation, as these documentation blocks are meant to be fairly short and enforcing one sentence per line may be quite unreadable in an indented comment block in a source file.

If substantial documentation is required for any Clockwork entity, it is better to provide a brief summary in the Clockwork source with a link to the complete documentation.

## Literals

Clockwork supports three kinds of literal values: integers, floating-point, and unit literals.

### Integers

Integers are composed of an optional sign, the digits 0-9, and optional digit separator `'`.
Here are some examples:

- `12345`
- `12'345`
- `-12'345`

The digit separator `'` is never required but is encouraged whenever the number of digits is greater than 4 for better readability.

Clockwork does not need suffixes (as in C++) to specify whether the integers are signed or unsigned or what the bit width is.
Clockwork has a pretty smart [type inference system](../guidance/type_inference.md) that will figure out the type of the literal based on context, and ensure that all types match and that the literal can be represented by the type required by the context.
Clockwork will never truncate, reinterpret, or lose precision in an integer literal because of type coercion.

Note that integer literals are never implicitly converted to floating point literals.
If you want `12345` as a floating point, that must be written `12345.0` instead.
Failure to do this will result in a type checking error from the Clockwork compiler.

There is currently no support for hexadecimal, octal, or binary integer literals.
This could be added easily if we encounter a use case for them, so speak up if you have such a use case.

### Floating point

Floating point literals follow familiar syntax options:

- `3.14159`
- `-1.0`
- `1.2345e3` or `1.2345E3`

You can also use the digit separator `'`, though this probably does not aid readability much in floating point literals; in most cases you'd be better off using the exponent form of the literal.

Note that Clockwork retains exact precision of floating point literals instead of immediately converting to binary floating point representation.
This prevents rounding errors that some other representations can suffer from, such as Yaml or JSON floating point numbers.
The exact precision you specify in Clockwork source survives in a decimal representation all the way to the point where we need to generate some other representation, at which point they may be rounded as required by that representation.

### Unit literals

Clockwork is built for safety, and keeping track of units is important for safety in a robotic system.
So Clockwork has a specific unit literal syntax, which is just a numeric literal with a unit suffix.
It can support both integer and floating point literals:

- `500ms` (500 milliseconds, `Duration` type)
- `3.45s` (3.45 seconds, `Duration` type)

Unit literals have both a unit and a strong type; in these examples the type is `Duration`, one of the built-in Clockwork types.
At the moment, `Duration`s are the primary unit literal you'll encounter in Clockwork source, but as we add additional strong types (such as `mps` for speed, and `m` for distance), we will add those with the same syntax.

## Constants

Clockwork allows you to define constants at the root of a module.
Constants can be used as default values for schema parameters and as arguments to schema instantiations.
Like other entities, constants can be imported from other modules with `use`.

The following are all valid constant definitions.
At the time of writing constants may only have primitive values.

- `foo = 1234;`
- `bar: Float32 = 1.234;`
- `baz = "a string";`

The `generate` attribute is used to make constants available in generated code.
In the example below, the constant `foo` would be available as `clockwork::testing::foo` and `testing_clk_py.FOO` respectively from the C++ and Python targets assuming the Clockwork source file was named `testing.clk`.

```clockwork
#![(generate(cpp, py)]
#![cpp(namespace=clockwork::testing)]

foo: UInt64 = 9999;
```
