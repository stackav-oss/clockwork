# Guidance on type inference in Clockwork

The Clockwork domain-specific language (DSL) compiler includes a type inference capability that makes explicit typing unnecessary in some contexts.
This document exists to provide guidance on when type inference should be used in Clockwork and when explicit typing should be used.
This document applies only to code written in the Clockwork DSL, and not to any other language.

## Clockwork type system basics

Readers not interested in justification for the guidance may skip straight to the [guidance](#guidance) section below, but this section explains how type inference works in Clockwork to help clarify why the guidance is as it is.

### Clockwork typing: No implicit conversions

Clockwork uses strong, static typing with no implicit type conversions, even when types are inferred rather than explicit.
Clockwork will not allow an `Int8` to be used implicitly in a context expecting an `Int16`, even though this is generally safe, and will never implicitly convert between signed and unsigned integers nor between integers and floating point numbers.
This already eliminates a large class of errors in C and C++ related to integer promotion and implicit conversions.

Clockwork uses contextual analysis to do type inference when explicit types are not provided.
That is, it looks at how a value is _used_, even after the value is declared, to determine its type.
The compiler ensures that all contexts in which a variable is used agree exactly on the type.

Clockwork also supports unit-associated values natively using strong types.
So for example a duration value has type `Duration`, which is distinct from all integer and floating point types, and distinct from all other types of units.
Duration literals look like, e.g., `3.5ms`, including the unit in the literal.
If you try to use a unitless literal like `3.5` in a context expecting a `Duration`, that is a compile-time error.

Enums and enum values are strongly typed in the Clockwork DSL and are not interchangeable with, nor even explicitly convertible to, integers.

### Safety vs readability

Guidance on C++ type inference via `auto` (see below) often focuses on safety, because many circumstances where C++ type inference can be used are inherently unsafe.
In Clockwork, type inference is designed to be inherently safe, due to lack of implicit conversions and forcing the type of every entity to be globally consistent in all contexts where it's used.

So the guidance here is based more on human factors than on compiler behavior.
The question we are concerned with is whether human engineers can understand the code clearly when reading it, and what the cognitive load is on the human reader.
In each context, we must ask whether an explicit type has higher, lower, or equal cognitive load for the human reader than an implicit type.
We must consider how much potential there is for confusion—not confusion of the Clockwork compiler, but of the human reading the code.

### Clockwork type inference vs C++ `auto`

The guidance here is different from the guidance on C++ `auto` types, particularly for integers.
In C++, implicit integer conversion and promotion is a "feature" of the language (though many modern programmers consider it more a bug than feature).
This creates some danger when using `auto` in combination with integer literals when the integer literal is not explicitly typed in some way.

As mentioned above, Clockwork never does implicit conversions.
In Clockwork, types never change unless explicitly changed by the programmer.
Therefore, type inference of numeric types specifically poses no real danger.
The only real consideration is readability, not correctness.

Another particular difference is that Clockwork does not infer types immediately when variables are declared, but considers the entire context of the source code.
So just providing an init value like `1024` does not cause the compiler to immediately try to infer the parameter type from that literal, as C++ would do with an `auto` variable:

```cpp
auto i = 3;
```

Here, the C++ compiler is going to immediately infer a type for `i` based solely on the integer literal.
It will infer a plain `int` as the type in this example.
Clockwork, though, waits until it sees how `i` is used before deciding on a type, and only then ensures that the initialization value is compatible with the type it inferred from other context.[^1]

[^1]:
    In fact, the type of integer and floating point literals is ultimately inferred from context as well, instead of being fixed when the compiler encounters the literal.
    The literal `3` in Clockwork could be quite a few different integer types, and the compiler will eventually pick one specific type for it, from context.

In this sense, it has much more in common with modern, safe type inference systems like Rust or Pyrefly than with C++'s `auto`.

## Guidance

### Size parameters: Generally use inference

Because Clockwork forces all containers to have a maximum size bound, we expect there to be a _lot_ of parameters in Clockwork schemas, mostly used to make these size bounds configurable.
This means that most parameters will have type `UInt64`.
Developers are encouraged for this specific type of parameter to allow the type to be inferred, so that `UInt64` doesn't clutter all the parameter definitions, because the fact that this is a size is generally entirely clear from context (the parameter name, the required documentation, and the default value if present).

For example:

```clockwork
parameters
{
  // Maximum number of tracks
  tracks_max_size = 30;
}
```

Even without the rest of the context of this schema, it is clear from the documentation and the parameter name that this is a size bound parameter.
Adding the type `UInt64` would probably increase cognitive load for the reader, or is at best neutral for cognitive load.

### Unit parameters with literal defaults: Inferred or explicit allowed

When a parameter has a unit-associated type, such as `Duration`, _and_ there is a default value provided as a unit literal, the type _may_ be omitted:

```clockwork
  // Timeout for incoming Tracks messages
  tracks_timeout = 120ms;
```

In this case, because of the unit literal (read as "120 milliseconds"), it is clear that this is a `Duration` parameter; if it were anything else the compiler would immediately fail on a type error.
The documentation and parameter name also make this clear.
Both human and compiler immediately and unambiguously know this type without further context.

However, in this context it's also entirely permissible to use an explicit type:

```clockwork
  // Timeout for incoming Tracks messages
  tracks_timeout: Duration = 120ms;
```

Cognitive load is probably largely unaffected either way for most readers.

If there is not a unit literal provided as a default value, then an explicit type is **required**, even though the compiler can usually infer the type from later context, because readers may only look at the parameter declaration and not at the contexts where it's used:

```clockwork
  // Timeout for incoming Tracks messages
  tracks_timeout: Duration;
```

### All other cases: Prefer explicit types

In all cases other than the ones explicitly mentioned above, prefer explicit type annotations when declaring parameters, fields, or other entities, whether the compiler requires them or not.
Readability will usually be enhanced rather than impeded by explicit types, because readers will not need to go in search of the context where the parameter is used to determine its type.

> [!NOTE]
> This applies only to declared, named entities in the source code.
> Literal values in the source code generally do not have nor need explicit type annotations.
