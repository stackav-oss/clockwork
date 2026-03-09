# DFL (Declarative Functional Language)

DFL (pronounced "duffel") is a pure-functional sublanguage within Clockwork for specifying runtime computations.

## What is DFL?

DFL provides a declarative, functional syntax for expressing computations that will be evaluated at runtime.
Unlike the main Clockwork DSL expressions which are evaluated at compile time, DFL expressions remain as code and are compiled to efficient C++ for runtime execution or analyzed statically.

DFL is designed for use cases such as:

- Signal transformations (filtering, smoothing, derived values)
- Detector conditions (fault detection predicates)
- Aligner constraints and objectives (message selection rules)
- Data flow specifications

## Language Principles

DFL follows these design principles:

- **Pure functional**: DFL computes values from values without side effects
- **Referentially transparent**: Same inputs always produce the same outputs
- **Statically typed**: Types are known at compile time and mostly inferred
- **Safe by construction**: Generated code has no undefined behavior, unbounded memory, or unbounded execution time
- **Not Turing-complete**: No iteration or recursion by design, enabling static analysis

## Expression Syntax

DFL supports standard mathematical and logical expressions with familiar syntax.

### Arithmetic Operators

```clk
a + b      // Addition
a - b      // Subtraction
a * b      // Multiplication
a / b      // Division
a % b      // Modulo (remainder)
-x         // Negation
+x         // Unary plus (identity)
|x|        // Absolute value
```

### Comparison Operators

```clk
a == b     // Equal
a != b     // Not equal
a < b      // Less than
a <= b     // Less than or equal
a > b      // Greater than
a >= b     // Greater than or equal
```

### Logical Operators

```clk
a and b    // Logical AND
a or b     // Logical OR
not a      // Logical NOT
```

### Operator Precedence

Operators follow standard mathematical precedence (highest to lowest):

- Unary: `-`, `+`, `|...|`, `not`
- Multiplicative: `*`, `/`, `%`
- Additive: `+`, `-`
- Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical AND: `and`
- Logical OR: `or`

Use parentheses to override precedence: `(a + b) * c`

### Literals

DFL supports the same literal syntax as the main Clockwork DSL:

- Integers: `42`, `-17`, `0xFF`
- Floating point: `3.14`, `-0.5`, `1e-6`
- Durations with units: `100ms`, `1.5s`, `500us`
- Strings: `"hello"`

### References

Simple identifiers reference named entities in scope:

```clk
speed
threshold
config.max_value
```

Member access uses dot notation to access fields of messages or other structured data:

```clk
message.header.timestamp
sensor_data.value
```

### Field Projection on Collections

When you access a field on a collection, the access is automatically mapped over all elements:

```clk
// If 'messages' is Collection<Message> and Message has field 'timestamp: SyncTime'
messages.timestamp    // Returns Collection<SyncTime>
```

This enables concise expressions over collections of structured data:

```clk
sum(sensors.value)      // Sum of values from collection of sensor data
count(messages)         // Number of messages in a collection
mean(readings.value)    // Average value from collection of readings
```

### Parentheses

Use parentheses to group expressions and override operator precedence:

```clk
(a + b) * c
(x > 0) and (y < 100)
```

### Tuple Literals

Tuples can be created using bracket syntax:

```clk
[1, 2, 3]                   // Tuple of integers
[a, b, c]                   // Tuple of values
[[1, 2], [3, 4]]            // Nested tuple
```

Tuples are used as:

- Arguments to aggregate functions like `sum`, `count`, etc.
- Collections for `map`, `filter`, and `flatten`
- Variadic function argument packs

### Inline Lambdas

Anonymous functions for use with `map` and `filter`:

```clk
fn(x) x * 2                // Double each element
fn(x) x > 0                // Predicate: is positive?
```

Lambdas are covered in detail in [Functions](functions.md#inline-lambdas).

## Further Reading

- [Conditional Expressions](conditionals.md) - `if-then-else`, `cond`, and `match` expressions
- [Match Expressions](match.md) - Pattern matching with `match`
- [Functions](functions.md) - User-defined functions
- [Built-in Functions](builtins.md) - Available built-in functions
- [Trait System](traits.md) - Type traits for operator support
