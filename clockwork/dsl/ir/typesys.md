# Clockwork DSL Type System and Type Inference

Clockwork uses a variant of the Hindley-Milner (HM) type system and type inference algorithm, with some simplifications and some extensions.
The type system is not currently complete, and in its current form is significantly simplified.
Inheritance of types is not supported.
Generic types (and generic functions) are supported.

## Type Values and Type Definitions

In the Clockwork DSL, types are first-class values.
There is one built-in type `Type`, which is the type of all type values;
the type of `Type` is also `Type`.
Types are represented in the Python IR with the `TypeVal` class, and most types are instances of `TypeDef` or a subclass.
A `TypeDef` is a `TypeVal` with a canonical unique name and identity in the system, accessible via the `fqn` (Fully Qualified Name) property.
Since types are first-class values, `TypeVal` is a subclass of `Value`, the class which represents all values in the IR.

## Type Inference

### Inference Basics

Every `Value` has a `type_info` attribute, which is either a `TypeVal` holding a concrete type, or an `InferenceVar` representing an inference variable used during HM type inference.
If the type is not known at the time of creation of the `Value`, then an `InferenceVar` will be created for it.
During type inference, these inference variables undergo "unification", which essentially means that pairs of inference variables are constrained to be identical (by creating unification constraints).
Inference variables can also be constrained to be identical to a concrete type.
If at any point unification constraints create a conflict, type inference fails.
Type inference succeeds when all inference variables are constrained to a concrete type.

In this style of type inference, types can be constrained bottom-up, top-down, "sideways", and any combination of those.
See [Inference Example](#inference-example) to understand how this works in a practical example.
See any reference on the [Hindley-Milner type system](https://en.wikipedia.org/wiki/Hindley%E2%80%93Milner_type_system) for all the gory details.

### Inference Extensions

We make a minor extension to the basic HM type system with a notion of a `NumericType` constraint.
This constrains a value to be either `Integer` or `Float`, without constraining it to a specific type of integer or floating point number (e.g., it does not constrain the bit width or signedness).

These extensions exist to implement convenient but safe inference of the types numeric literals, in a way similar to how Rust does it.
There are never implicit conversions between integer and floating point, but we don't infer the precise type immediately when the literal is encountered, only whether it's integer or floating point.
Type inference will in many cases constrain the literal to a specific type, at which point we'll check that the literal fits within the range of that type.
If type inference completes without constraining the type, we infer integers to be `Int32` and floating point to be `Float32`.

### Inference Example

To understand both how the system works and why we bothered, consider this expression:

```rust
let x: UInt64 = 1024;
let y = x + 1;
```

To make the first line work already requires type inference.
We have an unconstrained numeric literal; we could make this impossible and require every literal to be tagged with an explicit type, but that gets very verbose and cluttered (imagine seeing a type tag for the `1` literal on the following line!).

So assuming we want to allow this, the simplest thing would be to immediately apply a default type, such as Int32 (the default type in Rust, and a reasonable choice).
But then, assigning that to an explictly-typed variable `x` would fail unless we support implicit integer conversions, and we know from the dangers of C and C++ that we don't want to do that in a safety-oriented language, so that assignment would fail.

In Clockwork's type inference, when we first encounter the literal `1024` during bottom-up evaluation, we just create an inference variable and apply an `Integer` constraint to it, without selecting the specific concrete type yet.
Proceeding one level up evaluation, when we assign that to the explicitly-typed variable `x` we unify the inference variable with the type `UInt64`, which then constrains the type of the literal to be `UInt64`.
This is an example of a "top-down" contextual constraint.

The second line gets more interesting, because the constraints go both "sideways" and "up".
Neither the literal `1` nor the target variable `y` has an explicit type.
Proceeding bottom-up, we give both of them inference variables; in the case of the literal this will have an `Integer` constraint applied.
The `x` variable has a concrete type already.
The addition operator in Clockwork insists that both of its arguments have the same type—again because we don't like implicit conversions and integer promotion.
So that operator creates a unification of the types of its arguments, and between the arguments and the result type.
So now `1` is unified with `UInt64`, which traveled "sideways" through that operator (actually "up" and then "down").
And because the result of the operator is unified with the arguments, it's also `UInt64`, and this travels up and is unified with the inference variable for `y`, which now is also constrained to `UInt64`.

If we had not provided any type for `x` in this example, Clockwork would go with its default for integers (`Int32`).

Here are a few examples of things that would fail inference:

```rust
let x: Float64 = 1024;
```

This fails because we never do implicit conversions between floating point and integral types.
You would need this instead:

```rust
let x: Float64 = 1024.0;
```

And this fails for the same reason:

```rust
let x = 1024.0 + 1;
```

### Inference Implementation

Traditional HM inference creates constraint instances and then takes multiple passes to satisfy constraints.
We simplify this to essentially unifying inference variables immediately when the constraint would be created by linking them together into a linked list-like data structure.
This implementation may need to change to something more sophisticated when we implement inference of generic type parameters for functions and types, but it's not entirely clear we need that any time soon.
