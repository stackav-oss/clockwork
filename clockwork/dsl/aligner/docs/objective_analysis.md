# Objective Analysis

Objective analysis classifies how each input should be searched during alignment.
It provides per-level search type classification for the greedy join plan construction algorithm.

## Pipeline Position

```text
.clk source → Parse/IR → Type Check → Spec Extraction → STN Analysis → **Join Plan (calls Objective Analysis per level)** → ...
```

## Purpose

Given a set of objectives (`minimize(...)` / `maximize(...)`) and a bound set of already-placed inputs, this module classifies a single candidate input's search type.
The search type dictates what algorithm generated code uses to find the best message from that input.

## Search Types

| Type             | Meaning                                                       | Generated code complexity |
| ---------------- | ------------------------------------------------------------- | ------------------------- |
| `NEAREST`        | Start with the message closest to a bound input's field value | O(log N) binary search    |
| `LAST_IN_RANGE`  | Start with the last (maximum) message in the feasible window  | O(log N) upper bound      |
| `FIRST_IN_RANGE` | Start with the first (minimum) message in the feasible window | O(log N) lower bound      |
| `ANY_MATCH`      | Any message in the feasible window (no preference)            | O(log N) lower bound      |
| `ENUMERATE`      | Iterate over all messages in the feasible window              | O(N) scan                 |

## Algorithm

### Per-Level Classification

**Definition:** The _bound set_ is the set of all inputs that come before the current level; at runtime, these will already have specific candidate messages selected and can be considered fixed or bound in later levels.

At each level of the join plan, for a candidate input C with bound set B:

**Step A — Decompose objectives.**
For each objective involving C, decompose it into additive terms (flattening `Binary(ADD)` chains and `sum(ExprTuple(...))` patterns).
Each term is classified as _separable_ (references only C and members of B) or _non-separable_ (references C and one or more unbound inputs).

For example, consider the objective `minimize(|lidar.tov - camera.obs| + |lidar.tov - pose.tov|)` with bound set `B={pose}`.
When considering the candidate `lidar`:

- The term `|lidar.tov - pose.tov|` is separable: it references lidar (the candidate) and pose (bound)
- The term `|lidar.tov - camera.obs|` is non-separable: it references lidar (the candidate) and camera (unbound)

**Step B — Classify separable terms.**
Each separable term is pattern-matched to produce a search type:

- `(t_candidate - t_bound) * (t_candidate - t_bound)` (squared difference) → `NEAREST`
- `|t_candidate - t_bound|` (absolute difference) → `NEAREST`
- `maximize(t_candidate)` (simple field accessor) → `LAST_IN_RANGE`
- `minimize(t_candidate)` (simple field accessor) → `FIRST_IN_RANGE`
- Unrecognized patterns → `ENUMERATE`

**Step C — Merge classifications across objectives.**
A candidate may appear in multiple objectives.
The final search type is determined by merging all separable-term classifications:

| Situation                                                   | Result               |
| ----------------------------------------------------------- | -------------------- |
| No objectives reference C                                   | `ANY_MATCH`          |
| Only non-separable terms                                    | `ENUMERATE`          |
| Exactly one separable classification                        | That classification  |
| Multiple separable classifications, all the same type       | That type            |
| `NEAREST` + `FIRST_IN_RANGE` or `NEAREST` + `LAST_IN_RANGE` | `NEAREST` (subsumes) |
| `FIRST_IN_RANGE` + `LAST_IN_RANGE`                          | Error: conflicting   |

### Non-Separable Terms Are Ignored

Non-separable terms (referencing the candidate and unbound inputs) cannot be optimized at this level because the other variables are unknown.
Their existence does not prevent a candidate from receiving a non-ENUMERATE classification, provided at least one separable term exists.
The separable term provides the starting point; non-separable terms are deferred to later levels or handled by full iteration.

### Batch Boundary Defaults

Batch boundary inputs (`FirstInBatch`, `LastInBatch`) that would otherwise be classified as `ANY_MATCH` are promoted to `FIRST_IN_RANGE` and `LAST_IN_RANGE` respectively.
This provides a default behavior of making the batch as large as possible, if not otherwise specified.

### Let-Binding Resolution

Before analyzing objectives, all `Ref` nodes pointing to `AlignerLetBinding` values are replaced with their bound expressions.
This is necessary because `expand_all_calls` only expands user-defined function (`FnDef`) calls; let-binding references remain as indirections.

## Worked Example

With `maximize(lidar.tov)` and `minimize(spread(lidar.tov, camera.obs))`, constraints `lidar < pose` and `camera < pose`:

**Level 0 (bound={}):**

- lidar: `maximize(lidar.tov)` → `LAST_IN_RANGE`. spread term non-separable (camera unbound) → ignored.
- camera: spread term non-separable (lidar unbound) → `ENUMERATE`.
- pose: no objective → `ANY_MATCH`.

**Level 1 (bound={lidar}):**

- camera: `|lidar.tov - camera.obs|` now separable → `NEAREST`.
- pose: no objective → `ANY_MATCH`.

**Result:** lidar(`LAST_IN_RANGE`) → camera(`NEAREST`) → pose(`ANY_MATCH`).
Zero `ENUMERATE` inputs.

## API

Entry point: `classify_input(candidate, bound_inputs, objectives, active_inputs)`.
Returns a `SearchClassification` with `search_type` and `objective_term`.
