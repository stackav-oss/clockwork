# DFL Conditional Expressions

DFL provides several ways to express conditional logic.

## If-Then-Else

The simplest conditional expression uses `if`, `then`, and `else`:

```clk
if condition then value_if_true else value_if_false
```

The condition must be a `Bool` expression.
Both branches must have the same type, which becomes the type of the entire expression.

### Examples

```clk
if speed > 0 then distance / speed else 0

if sensor.is_valid then sensor.value else default_value

if x < 0 then -x else x    // Same as |x|
```

### Nesting

If-then-else expressions can be nested:

```clk
if x < 0 then "negative"
else if x == 0 then "zero"
else "positive"
```

## Cond Expressions

For multiple conditions, `cond` provides cleaner syntax than nested if-then-else:

```clk
cond {
  condition1 => result1,
  condition2 => result2,
  ...
  else => default_result,
}
```

Conditions are evaluated in order.
The result of the first matching condition is returned.
The `else` arm is required and handles all remaining cases.

### Syntax

```clk
cond {
  guard_expr => body_expr,
  ...
  else => body_expr,
}
```

- Each arm consists of a guard expression and a body expression, separated by `=>`
- Arms are separated by commas (trailing comma is optional)
- The `else` arm must be last

### Examples

```clk
cond {
  speed < 0 => ErrorCode::invalid,
  speed < min_speed => ErrorCode::too_slow,
  speed > max_speed => ErrorCode::too_fast,
  else => ErrorCode::ok,
}
```

```clk
cond {
  temperature > 100 => "critical",
  temperature > 80 => "warning",
  temperature > 60 => "elevated",
  else => "normal",
}
```

### Type Requirements

- All guard expressions must be `Bool`
- All body expressions must have the same type

## Match Expressions

For matching against specific values (especially enums), see [Match Expressions](match.md).
