# DFL Match Expressions

Match expressions provide multi-way branching based on value patterns, similar to switch statements in other languages but more powerful.

## Basic Syntax

```clk
match scrutinee {
  pattern1 => result1,
  pattern2 => result2,
  ...
}
```

The scrutinee (the value being matched) is compared against each pattern in order.
The result of the first matching arm is returned.

## Pattern Types

### Literal Patterns

Match against specific literal values:

```clk
match error_code {
  0 => "success",
  1 => "warning",
  2 => "error",
  else => "unknown",
}
```

### Enum Patterns

Match against enum variants using `Enum::Variant` syntax:

```clk
match sensor.status {
  SensorStatus::ok => 1.0,
  SensorStatus::degraded => 0.5,
  SensorStatus::failed => 0.0,
}
```

### Range Patterns

Match against a range of integer values (inclusive):

```clk
match error_code {
  0 => "none",
  1..10 => "minor",
  11..100 => "major",
  else => "critical",
}
```

Range patterns use the syntax `lo..hi` where both bounds are inclusive integers.

### Union Patterns

Multiple patterns can be combined with `|` to match any of them:

```clk
match day {
  0 | 6 => "weekend",
  1..5 => "weekday",
  else => "invalid",
}
```

### Else Pattern

The `else` pattern matches anything not covered by other patterns:

```clk
match value {
  0 => "zero",
  1 => "one",
  else => "other",
}
```

## Exhaustiveness Checking

When matching on enums, the compiler checks that all variants are covered.

### Complete Coverage

If all enum variants are matched, no `else` is needed:

```clk
match sensor.status {
  SensorStatus::ok => 1.0,
  SensorStatus::degraded => 0.5,
  SensorStatus::failed => 0.0,
}  // Complete - all variants covered
```

### Incomplete Coverage

If not all variants are covered, an `else` arm is required:

```clk
match sensor.status {
  SensorStatus::ok => "operational",
  else => "needs attention",
}
```

### Missing Variants Error

If you omit variants without providing `else`, a compile error occurs:

```clk
match sensor.status {
  SensorStatus::ok => 1.0,
  SensorStatus::degraded => 0.5,
  // ERROR: Missing SensorStatus::failed
}
```

## Type Requirements

- All body expressions must have the same type
- The scrutinee type must be compatible with all patterns
- Enum patterns must reference valid variants of the scrutinee's enum type

## Examples

### Mapping Error Codes to Severities

```clk
match error_code {
  0 => Severity::none,
  1..10 => Severity::warning,
  11..100 | 255 => Severity::error,
  else => Severity::unknown,
}
```

### Converting Enum to Numeric Value

```clk
match gear {
  Gear::park => 0,
  Gear::reverse => -1,
  Gear::neutral => 0,
  Gear::drive => 1,
  Gear::low => 1,
}
```

### Categorizing Values

```clk
match temperature {
  0..32 => "freezing",
  33..50 => "cold",
  51..70 => "mild",
  71..85 => "warm",
  86..100 => "hot",
  else => "extreme",
}
```
