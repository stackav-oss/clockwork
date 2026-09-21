# Spec Extraction

Spec extraction is the compiler phase that transforms type-checked aligner body expressions into structured constraint, equality, and objective IR.
It takes the expanded DFL expression trees produced by type checking and normalizes them into `DifferenceConstraint`, `EqualityConstraint`, and `Objective` records suitable for downstream analysis.

## Pipeline Position

Spec extraction sits between type checking and STN (Simple Temporal Network) construction:

```text
.clk source → Parse/IR → Type Check → Spec Extraction → STN Analysis
```

The type checker (`type_check.py`) expands all function calls and stores the result on each `AlignerSpecStmt` as `expanded_expr`.
Spec extraction (`extract_specs.py`) walks those expanded expressions to produce an `ExtractedSpecs` value.

## Key Data Structures

All data structures are defined in `extract_specs.py`.

### FieldAccessor

Identifies a field on an aligner input, e.g., `("lidar1", "observation_time")`.
Field accessors are not limited to timestamp fields — any field whose type supports the relevant arithmetic (subtraction, comparison) is valid.
The type system has already validated this during type checking, so the accessor is purely structural.

### DifferenceConstraint

A normalized constraint of the form `minuend - subtrahend <= bound`.
Only produced when both fields are declared sorted (`is_strictly_increasing` or `is_non_decreasing`) for equality, or for explicit ordering constraints.
The `source_expr` field retains the original DFL expression for error reporting.

### EqualityConstraint

A simple `left == right` equality on fields that may not be orderable.
Produced when `require(a.f == b.f)` references fields that are not both declared sorted.

### Objective

A `(sense, expr)` pair extracted from `minimize(...)` or `maximize(...)` calls.
Objectives are stored without further decomposition — separability analysis is a future phase.

### ExtractedSpecs

The top-level output of spec extraction, containing:

- All field accessors referenced in the aligner.
- Field assumptions declared via `assume()` calls.
- Unconditional constraints, equalities, and objectives (always active).
- Conditional constraints, equalities, and objectives (gated by `has_candidates(...)` on an optional input).
- Else-branch constraints, equalities, and objectives (active when an optional input is absent).

## Field Assumptions

Users declare field properties with `assume()`:

```text
assume(is_strictly_increasing(lidar.observation_time));
assume(is_non_decreasing(imu.tov));
assume(is_unique(camera.tag_id));
```

These are collected in pass 1 and used during pass 2 to:

- **Classify equality constraints**: `require(a == b)` with both fields declared sorted (`is_strictly_increasing` or `is_non_decreasing`) produces `DifferenceConstraint` pair; otherwise `EqualityConstraint`.
- **Validate ordering constraints**: `require(a <= b)`, `require(|a - b| <= d)`, etc. require all referenced fields to be declared sorted (`is_strictly_increasing` or `is_non_decreasing`).

Canonicalization: declaring `is_non_decreasing` together with `is_unique` on the same field is equivalent to `is_strictly_increasing` and is stored as such.
Declaring `is_non_decreasing` and `is_strictly_increasing` on the same field is redundant and produces an error.

## Constraint Normalization

The `require(...)` builtin accepts several expression patterns, each of which maps to constraints.

| Aligner source            | Field assumptions | Normalized constraints               |
| ------------------------- | ----------------- | ------------------------------------ |
| `require(a == b)`         | Both sorted       | `a - b <= 0` and `b - a <= 0` (Diff) |
| `require(a == b)`         | Not both sorted   | `EqualityConstraint(a, b)`           |
| `require(a <= b)`         | Both sorted       | `a - b <= 0`                         |
| `require(a >= b)`         | Both sorted       | `b - a <= 0`                         |
| `require(\|a - b\| <= d)` | Both sorted       | `a - b <= d` and `b - a <= d`        |

"Sorted" means either `is_strictly_increasing` or `is_non_decreasing`.
Ordering constraints on fields that are not declared sorted produce a `SpecExtractionError` with guidance to add the missing assumption.

## Compound Specs

When multiple specs are composed with `and`, the type checker expands this into `Binary(AND, left, right)`.
The extractor recursively walks both sides, flattening the tree and collecting all constituent constraints and objectives.

For example, `require(a == b) and require(b <= c)` produces three difference constraints (two from equality, one from ordering), assuming all fields are declared sorted.

## Conditional Specs

Aligner bodies can gate specs on whether an optional input has data:

```text
if has_candidates(radar) then
    require(|lidar.t - radar.t| <= 100ms)
else
    require(true)
```

The extractor recognizes `IfElse` nodes whose condition is a `has_candidates(...)` call.
Constraints and objectives from the `then` branch are tagged as conditional on that input being present.
Constraints and objectives from the `else` branch are stored separately in `else_constraints`/`else_objectives`.
In most cases the `else` branch is `require(true)`, which produces no constraints.

## Entry Point

The public API is `extract_specs(aligner_node)`, which takes a parsed and type-checked `Aligner` IR node and returns an `ExtractedSpecs`.
It iterates over the aligner's body statements in two passes (assumptions first, then constraints/objectives).
`AlignerLetBinding` and `FnDef` statements are skipped — let bindings are resolved during expansion, and function definitions are inlined at their call sites.
