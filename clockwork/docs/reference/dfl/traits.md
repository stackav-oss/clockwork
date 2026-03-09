# DFL Trait System

DFL uses a trait system to declare which operations are supported on which types.
Traits enable type checking of operators and functions at compile time, ensuring type safety.

## Overview

The trait system serves two purposes:

- **Type checking**: Verify that operators and functions are applied to compatible types
- **Type inference**: Determine the result type of operations

Traits do not contain implementation code.
The actual operations are implemented in C++; traits simply describe what operations exist and what types they produce.

## Trait Definitions

A trait defines an abstract capability that types can implement.

```clk
trait Add<Rhs = Self> {
  type Output;
}
```

This defines a trait named `Add` with:

- A type parameter `Rhs` that defaults to `Self` (the implementing type)
- An associated type `Output` that implementations must specify

### Syntax

```clk
trait TraitName ;
trait TraitName<TypeParam, ...> ;
trait TraitName<TypeParam = Default, ...> {
  type AssociatedType;
  ...
}
```

Components:

- `TraitName`: The name of the trait
- `<TypeParam, ...>`: Optional type parameters (can have defaults using `= Type` or `= Self`)
- `type AssociatedType;`: Associated types that implementations must define

The `Self` keyword refers to the type implementing the trait.

## Impl Declarations

An `impl` declaration states that a type implements a trait.

```clk
impl Add for Int64 { type Output = Int64; }
impl Add<Float64> for Duration { type Output = Duration; }
impl Ord for Float64;
```

### Syntax

```clk
impl TraitRef for Type ;
impl TraitRef for Type { type AssocType = ConcreteType; ... }
```

Components:

- `TraitRef`: The trait being implemented, optionally with type arguments (e.g., `Add<Float64>`)
- `for Type`: The type that implements the trait
- `type AssocType = ConcreteType;`: Definitions for associated types

## Built-in Traits

Clockwork provides built-in traits for common operations.
These are defined in `@clockwork::std::traits`.

### Arithmetic Traits

| Trait      | Operators | Associated Types |
| :--------- | :-------- | :--------------- |
| `Add<Rhs>` | `+`       | `Output`         |
| `Sub<Rhs>` | `-`       | `Output`         |
| `Mul<Rhs>` | `*`       | `Output`         |
| `Div<Rhs>` | `/`       | `Output`         |
| `Rem<Rhs>` | `%`       | `Output`         |
| `Neg`      | unary `-` | `Output`         |

### Comparison Traits

| Trait      | Operators            | Associated Types |
| :--------- | :------------------- | :--------------- |
| `Ord<Rhs>` | `<`, `<=`, `>`, `>=` | (none)           |
| `Eq<Rhs>`  | `==`, `!=`           | (none)           |

Comparison operators always return `Bool`.

## Type Checking with Traits

When DFL type-checks a binary operation like `a + b`:

- It looks up the types of `a` and `b`
- It searches for `impl Add<typeof(b)> for typeof(a)`
- If found, the result type is the `Output` associated type
- If not found, a type error is reported

### Example

```clk
// Given these impls:
impl Add for Float64 { type Output = Float64; }
impl Add for Duration { type Output = Duration; }
impl Div<Duration> for Duration { type Output = Float64; }

// These expressions type-check:
1.5 + 2.5           // Float64 + Float64 → Float64
100.ms + 50.ms      // Duration + Duration → Duration
100.ms / 50.ms      // Duration / Duration → Float64

// This would be a type error:
1.5 + 100.ms        // No impl Add<Duration> for Float64
```

## Defining Custom Trait Implementations

When working with custom types, you may need to declare trait implementations to use them with DFL operators.

```clk
// Define a strong type
strong_type Celsius {
  underlying_type: Float64;
}

// Declare what operations are supported
impl Add for Celsius { type Output = Celsius; }
impl Sub for Celsius { type Output = Celsius; }
impl Neg for Celsius { type Output = Celsius; }
impl Ord for Celsius;
impl Eq for Celsius;
```

This allows DFL expressions to use arithmetic and comparison operators on `Celsius` values.
