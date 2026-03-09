# DFL User-Defined Functions

DFL allows you to define reusable expression templates using the `fn` syntax.

## Basic Syntax

```clk
fn function_name(param1, param2, ...) {
  expression
}
```

Functions are **templates** that are expanded by substitution before type checking, similar to C++ templates or Lisp macros.

## Examples

### Simple Function

```clk
fn double(x) {
  x + x
}

// Usage:
double(5)       // Expands to: 5 + 5
double(3.14)    // Expands to: 3.14 + 3.14
```

### Multi-Parameter Function

```clk
fn clamp(value, lo, hi) {
  if value < lo then lo
  else if value > hi then hi
  else value
}

// Usage:
clamp(speed, 0, 100)
```

### Mathematical Formulas

```clk
fn celsius_to_fahrenheit(c) {
  c * 1.8 + 32.0
}

fn distance(x1, y1, x2, y2) {
  |x2 - x1| + |y2 - y1|    // Manhattan distance
}
```

## Type Annotations

Type annotations are optional but can be added for documentation:

```clk
fn celsius_to_fahrenheit(c: Float64) -> Float64 {
  c * 1.8 + 32.0
}
```

### Syntax

```clk
fn name(param: Type, ...) -> ReturnType { ... }
```

Type annotations do not change behavior.
The function body is still type-checked after expansion at each call site.

## Variadic Functions

Functions can accept a variable number of arguments using the `...` suffix:

```clk
fn sum_all(values...) {
  sum(values)
}

// Usage:
sum_all(1, 2, 3)      // values becomes [1, 2, 3]
sum_all(a, b, c, d)   // values becomes [a, b, c, d]
```

### Variadic Parameter Rules

- The variadic parameter must be the last parameter
- Inside the function, the variadic parameter behaves like a tuple of values
- Use with aggregate functions like `sum`, `min`, `max`, etc.

### Mixed Parameters

```clk
fn biased_sum(bias, values...) {
  bias + sum(values)
}

// Usage:
biased_sum(10, 1, 2, 3)    // 10 + sum([1, 2, 3]) = 16
```

## Calling Functions

### Positional Arguments

```clk
clamp(speed, 0, 100)
```

### Named Arguments

Arguments can be named for clarity:

```clk
clamp(value=speed, lo=0, hi=100)
```

Named arguments can appear in any order:

```clk
clamp(hi=100, lo=0, value=speed)
```

### Spread Operator

Use `...` to expand a tuple into individual arguments:

```clk
fn my_max(values...) {
  max(values...)    // Forwards the tuple as separate args to max
}
```

## Template Semantics

Functions are expanded at each call site before type checking:

```clk
fn double(x) { x + x }

// These expand to different types:
double(5)       // 5 + 5 : Int64
double(3.14)    // 3.14 + 3.14 : Float64
double(1.s)     // 1.s + 1.s : Duration
```

Type errors are reported at the call site with the expanded expression:

```clk
double("hi")    // ERROR: No impl Add for String
```

## Inline Lambdas

DFL supports inline lambda expressions using the `fn(params) body` syntax.
Lambdas are anonymous functions for use with `map` and `filter`.

### Syntax

```clk
fn(x) x * 2                    // Single parameter
fn(x, y) x + y                 // Multiple parameters
fn() 42                        // No parameters
```

### Using Lambdas with map()

```clk
map(fn(x) x * 2, [1, 2, 3])    // Returns [2, 4, 6]
map(fn(x) x > 0, values)       // Returns Collection<Bool>
```

### Using Lambdas with filter()

```clk
filter(fn(x) x > 0, values)    // Keep positive values
filter(fn(x) x != 0, values)   // Remove zeros
```

### Capturing Outer Scope Values

Unlike named functions, lambdas can reference values from their enclosing scope:

```clk
fn squared_diff(ref, times...) {
    sum(map(fn(t) (t - ref) * (t - ref), times))
}
```

In this example:

- `t` is the lambda parameter (bound to each element of `times`)
- `ref` is captured from the outer function's parameters

The captured value `ref` is substituted before the lambda is applied, so:

```clk
squared_diff(r, [a, b, c])
// After outer function expansion:
//   sum(map(fn(t) (t - r) * (t - r), [a, b, c]))
// After map expansion:
//   sum([(a - r) * (a - r), (b - r) * (b - r), (c - r) * (c - r)])
```

Note that DFL is inherently a value-based language; every value is immutable and there are no side effects.
Therefore "capturing" is like capturing by value, not by reference.
The capture turns into a simple substitution, which in fact _duplicates_ the captured value in many cases.
Because this is a pure-value language, there can be no side effects of this, no aliasing problems, etc.

### Lambda Restrictions

- **Single expression body**: The body is a single expression (no statements)
- **Immediate use only**: Lambdas cannot be stored in variables
- **No recursion**: Lambda bodies cannot reference themselves
- **Single-parameter for map/filter**: Lambdas passed to `map` or `filter` must have exactly one parameter

## General Restrictions

- **No recursion**: Functions cannot call themselves directly or indirectly
- **No closures**: Except for lambdas, which cannot escape their scope, functions cannot capture variables from their defining scope
- **Pure expressions only**: No side effects or statements
