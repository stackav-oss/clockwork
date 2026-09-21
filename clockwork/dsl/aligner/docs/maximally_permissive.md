# Maximally Permissive Windows

## Purpose

Codegen needs initial search windows for each required-input pair that are wide enough for any candidate that could participate in a feasible alignment under any possible runtime presence combination.
These are the **maximally permissive** windows.

The constraint set active at runtime depends on which optionals are present — through auto-drop (removing constraints referencing absent non-`has_candidates` optionals) and through `has_candidates` conditionals (which substitute different constraints based on presence).
But the initial search window cannot know what optionals will be present, other than those that are earlier in the join plan, which is why we start with the widest possible search window.

## Why Not Just All-Absent?

If there are no `has_candidates` conditionals, then dropping an optional input can only remove constraints, never add tighter ones.
So you'd get the maximally permissive windows by treating all optionals as absent.

A `has_candidates` else-branch can impose a _tighter_ constraint than its then-branch.
Example: `if has_candidates(O) then require(|R1-R2| <= 500ms) else require(|R1-R2| <= 100ms)`.
All-absent gives 100ms; O-present gives 500ms.
The maximally permissive window is 500ms, from the O-present config.

So for optional inputs _without_ a `has_candidates`, they are covered by starting with the all-absent config.
But then after that we need to consider all combinations of `has_candidates` optionals as present vs absent to find the true maximally permissive windows.

## Algorithm

Let k = number of optionals with `has_candidates` conditionals.
There are 2^k constraint-set variants.

For each variant:

- Start with unconditional constraints, filtered to required-only inputs (the all-absent config).
- For each `has_candidates` optional, include either its then-branch or else-branch constraints, filtered to required-only.
- Run `analyze_stn()` and extract feasible windows.
- Accumulate into the running result by element-wise relaxation (widest bounds).

Relaxation means:

- `lo = min(existing_lo, variant_lo)` — most negative lower bound.
- `hi = max(existing_hi, variant_hi)` — most positive upper bound.
- `None` (unbounded) always wins as most permissive.

Non-`has_candidates` optionals are always treated as absent.
Their auto-drop only removes constraints, never adds tighter ones.

## Implementation

The function `compute_maximally_permissive_windows()` lives in `presence.py`.
It takes the raw `ExtractedSpecs` and `optional_inputs`, returning a dict mapping `(target, reference)` accessor pairs to `FeasibleWindow` values.

The result is stored on `AlignerAnalysis.maximally_permissive_windows` by the pipeline.

## See Also

- [STN Analysis](stn_analysis.md) — How the underlying STN computation works.
- [Spec Extraction](spec_extraction.md) — How constraints are extracted and partitioned by `has_candidates`.
