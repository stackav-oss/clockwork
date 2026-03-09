# DFL Built-in Functions

DFL provides several built-in functions for common operations.

## Scalar Functions

### min / max

Return the minimum or maximum of their arguments.

```clk
min(a, b)           // Minimum of two values
min(a, b, c)        // Minimum of three values
max(x, y, z)        // Maximum of three values
```

These functions are variadic and accept two or more arguments.
All arguments must have the same type, and the result has that type.

### abs

Return the absolute value.

```clk
abs(x)              // Equivalent to |x|
abs(-5)             // 5
abs(3.14)           // 3.14
```

### clamp

Constrain a value to a range.

```clk
clamp(value, lo, hi)
```

Returns `lo` if `value < lo`, `hi` if `value > hi`, otherwise `value`.

```clk
clamp(speed, 0, 100)    // Ensure speed is between 0 and 100
clamp(-5, 0, 10)        // 0
clamp(50, 0, 10)        // 10
clamp(5, 0, 10)         // 5
```

## Collection Aggregates

These functions operate on collections (tuples or arrays) and return a single value.

When combined with [field projection](README.md#field-projection-on-collections), you can easily aggregate over structured data:

```clk
// Sum values from sensor readings
sum(readings.value)

// Count elements in a collection
count(messages)
```

### sum

Sum all elements in a collection.

```clk
sum([1, 2, 3])          // 6
sum(values)             // Sum of all values in collection
```

Elements must implement the `Add` trait.

### count

Return the number of elements in a collection.

```clk
count([1, 2, 3])        // 3
count(messages)         // Number of messages
```

Always returns `UInt64`.

### mean

Return the arithmetic mean of elements.

```clk
mean([1.0, 2.0, 3.0])   // 2.0
mean(readings)          // Average of readings
```

Always returns `Float64`.

### any / all

Boolean aggregates for collections of `Bool`.

```clk
any([true, false, false])   // true (at least one is true)
all([true, false, true])    // false (not all are true)
all([true, true, true])     // true
```

## Collection Transformers

These functions transform collections into new collections.

### map

Apply a function to each element of a collection.

```clk
map(func, collection)
```

The first argument can be a function reference or an inline lambda (see [Functions](functions.md#inline-lambdas)).

```clk
fn double(x) { x * 2 }

// Using a named function
map(double, [1, 2, 3])      // [2, 4, 6]
map(abs, [-1, 2, -3])       // [1, 2, 3]

// Using an inline lambda
map(fn(x) x * 2, [1, 2, 3])       // [2, 4, 6]
map(fn(x) x > 0, [-1, 2, -3])     // [false, true, false]
```

### filter

Select elements that satisfy a predicate.

```clk
filter(predicate, collection)
```

The predicate must return `Bool`.
The first argument can be a function reference or an inline lambda.

```clk
// Using a named predicate
filter(is_positive, [-1, 2, -3, 4])    // [2, 4]

// Using an inline lambda
filter(fn(x) x > 0, [-1, 2, -3, 4])    // [2, 4]
filter(fn(x) x != 0, [-1, 0, 3])       // [-1, 3]
```

### flatten

Collapse one level of nesting in a collection of collections.

```clk
flatten([[1, 2], [3, 4]])   // [1, 2, 3, 4]
```

## Predicate Functions

These functions return `Bool` and are useful with `filter`.

### is_positive

Returns `true` if the value is greater than zero.

```clk
is_positive(5)      // true
is_positive(-3)     // false
is_positive(0)      // false
```

### is_negative

Returns `true` if the value is less than zero.

```clk
is_negative(-5)     // true
is_negative(3)      // false
is_negative(0)      // false
```

### is_zero

Returns `true` if the value equals zero.

```clk
is_zero(0)          // true
is_zero(5)          // false
is_zero(-3)         // false
```
