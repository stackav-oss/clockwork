# STN Analysis

STN analysis is what allows the compiler to figure out transitive constraints involving three or more inputs, and also to eliminate redundant constraints.
It works by transforming extracted difference constraints into a Simple Temporal Network and computing tightest bounds via the Floyd-Warshall pairwise shortest-path algorithm.
It takes the `DifferenceConstraint` records produced by spec extraction and produces an `StnAnalysis` containing a distance matrix, feasible windows, and equality classes.

Only `DifferenceConstraint`s enter the STN — these are constraints on fields declared sorted (either `is_strictly_increasing` or `is_non_decreasing`) via `assume()`.
`EqualityConstraint`s (on non-ordered fields) are handled separately and do not participate in STN analysis.

## Why STN?

Aligner constraints are written pairwise (`require(|a - b| <= 100ms)`), but the compiler needs to reason about _all_ pairs of inputs — including pairs with no explicit constraint.
A Simple Temporal Network (STN) is a well-studied representation for this kind of problem: given a set of pairwise difference bounds, what are the tightest implied bounds between every pair?

For example, if `|lidar - camera1| <= 100ms` and `|camera1 - camera2| <= 100ms`, then transitively `|lidar - camera2| <= 200ms`.
The STN encodes all constraints as a weighted directed graph and lets us compute all such transitive bounds in one pass.

Downstream compiler phases use these tightest bounds to:

- Determine the search space for each input during alignment (the feasible window)
- Identify inputs that must have identical values (equality classes), enabling lookup instead of search
- Detect contradictory constraints before generating runtime code

## Algorithm Overview

### STN Construction

The STN is a weighted directed graph where:

- **Nodes** are field accessors (`input.field` pairs), e.g., `(lidar1, observation_time)`.
- **Edges** represent difference constraints: an edge from _B_ to _A_ with weight _w_ means `A - B <= w`.

Each `DifferenceConstraint(A, B, w)` becomes a directed edge from _B_ to _A_ with weight _w_.

### Floyd-Warshall

Floyd-Warshall is a classic algorithm that computes the shortest path between every pair of nodes in a weighted graph.
In the STN, the shortest path from _B_ to _A_ gives the tightest upper bound on `A - B`.
The key insight is that shortest-path composition corresponds to constraint composition: if `A - B <= 10` and `B - C <= 20`, then the path B→A (weight 10) composed with the path C→B (weight 20) gives C→A (weight 30), meaning `A - C <= 30`.

This also eliminates redundant constraints: if a direct constraint `A - C <= 50` exists and a transitive constraint is tighter, so that the shortest path gives `A - C <= 30` as in the example above, then the explicit constraint is redundant and can be ignored in downstream analysis.

The pairwise shortest paths are recorded in a distance matrix where `d[i][j]` gives the tightest upper bound on `node_i - node_j`.
A value of `None` means no constraint exists between those nodes (infinite bound).

For typical aligners with fewer than 20 nodes, this computation is trivially fast.

### Consistency Check

After Floyd-Warshall, the diagonal of the distance matrix reveals whether the constraints are self-consistent.
`d[i][i]` represents the tightest bound on `node_i - node_i`, which must be zero for consistent constraints.
A negative `d[i][i]` means the constraints imply that a value must be strictly less than itself — a contradiction.
This happens when a negative-weight cycle exists in the graph (the constraint chain loops back with a net negative bound).

### Equality Class Detection

Two nodes _i_ and _j_ are in the same equality class if `d[i][j] == 0` and `d[j][i] == 0`.
This means a constraint (direct or transitive) forces them to be exactly equal.
A union-find structure groups equal nodes into classes.

## Reading the Distance Matrix

The distance matrix `d[i][j]` answers: "What is the maximum amount that node _i_ can exceed node _j_?"

| Value            | Meaning                                              |
| ---------------- | ---------------------------------------------------- |
| `d[i][j] = 0`    | `i` cannot exceed `j` (i.e., `i <= j`)               |
| `d[i][j] = 0.1`  | `i` can be at most 0.1 seconds (100ms) ahead of `j`  |
| `d[i][j] = None` | No constraint limits how far `i` can be ahead of `j` |

The **feasible window** for `i - j` is `[-d[j][i], d[i][j]]`.
Both bounds can be `None` (unconstrained).

## Bound Normalization

Constraint bounds are stored as `UnitValue | DecimalValue` in `DifferenceConstraint`.
The STN normalizes all bounds to `Decimal` values for arithmetic:

| Bound type                  | Normalization                                                  |
| --------------------------- | -------------------------------------------------------------- |
| `UnitValue` (e.g., `100ms`) | Convert to canonical unit (seconds for time): `Decimal('0.1')` |
| `DecimalValue` (e.g., `5`)  | Use raw `Decimal` value                                        |

The type checker ensures that constraints compare compatible types, so within any connected component of the STN, all edge weights are in the same unit system.

## Entry Point

The public API is `analyze_stn(constraints)`, which takes an iterable of `DifferenceConstraint` records and returns an `StnAnalysis`.
The caller decides which constraints to pass — typically the unconditional constraints, or the unconditional plus one set of conditional constraints for a particular presence configuration.
