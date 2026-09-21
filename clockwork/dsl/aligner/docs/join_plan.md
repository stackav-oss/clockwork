# Join Plan Computation

Join plan computation is the compiler pipeline stage that determines the nesting order and search strategy for each input using a greedy level-by-level algorithm.
It runs objective analysis at each step for objective-derived search types, then applies constraint-derived overrides (EXACT_MATCH detection) before scoring.

## Pipeline Position

```text
.clk source → Parse/IR → Type Check → Spec Extraction → STN Analysis → **Join Plan** → Code Gen → ...
```

## Purpose

Given inputs, objectives, STN analysis, equality constraints, and field assumptions, this stage produces an ordered sequence of **join levels** that directly maps to the generated search code.
Each level specifies which input to bind, what search strategy to use, what STN-derived feasible windows constrain the search, and what equality-filter checks to apply.

## Algorithm

### Greedy Level-by-Level Construction

The algorithm builds the join plan one level at a time:

- **Initialize**: All inputs start unbound.
- **At each step**: For every unbound input, call `objective_analysis.classify_input()` with the current bound set to determine its objective-derived search type, then check for EXACT_MATCH via constraint analysis, then score the candidate.
- **Pick the best**: Place the highest-scoring candidate as the next join level.
- **Update**: The placed input joins the bound set for subsequent levels.
- **Repeat** until all inputs are placed.

The first placed input becomes the outermost iteration.

### EXACT_MATCH Detection

After objective-derived classification, the algorithm checks whether the candidate has an equality relationship with a bound input on a sorted field.
Two paths:

- **STN equality**: Both fields are declared sorted (`is_strictly_increasing` or `is_non_decreasing`), and the STN detects they are exactly equal (zero-bounded difference constraints).
  Uses `StnAnalysis.are_equal()`.
- **EqualityConstraint with sorted target**: The candidate's field is declared sorted, and an `EqualityConstraint` links it to a bound input.

In either case, the search type is overridden to EXACT_MATCH: binary search narrows the view to the window of matching values, and the forward scan iterates any duplicates (a single candidate is produced for strictly-increasing targets, all matches are produced for non-decreasing targets).

### Equality Checks

For `EqualityConstraint`s where the target field is not declared sorted, the constraint becomes an equality-filter check attached to the `JoinLevel`.
These filters do not change the search type but provide additional candidate filtering.
The `is_unique` property (from field assumptions) determines whether codegen can stop after the first match.

### Scoring Function

Candidates are scored by a tuple (lower is better, Python ascending sort):

- **Required vs optional**: Required inputs (0) before optional inputs (1).
- **Search type rank**: Better search types first (EXACT_MATCH=0 > NEAREST=1 > FIRST/LAST_IN_RANGE=2 > ANY_MATCH=3 > ENUMERATE=4).
- **Equality quality**: Unique equality filter (0) > non-unique equality filter (1) > no equality (2).
- **Connectivity to unbound**: Higher connectivity preferred (placing a well-connected input makes more terms separable at subsequent levels).
- **Buffer size**: Smaller `max_msgs` first (cheaper ENUMERATE loops).
- **Alphabetical**: Tiebreak on input name for determinism.

### Dependency Graph

The module builds a constraint dependency graph from STN edges and equality constraints.
Two inputs are connected if any STN edge or `EqualityConstraint` links field accessors belonging to different inputs.
The `depends_on` field of each join level records which bound inputs share constraints with it.

### Feasible Windows

For each placed level, the module computes STN-derived feasible windows relative to all already-bound inputs.
These windows contain the lower and upper bounds on the timestamp difference, which code generation uses to narrow the search range.

## API

Entry point: `compute_join_plan(inputs=..., objectives=..., active_inputs=..., stn=..., aligner_node=..., equalities=..., field_assumptions=...)`.
Returns a `JoinPlan` with ordered `JoinLevel` values.

### Key Types

- `JoinPlan` — the complete plan (ordered levels)
- `JoinLevel` — one level (input, search type, `objective_term`, dependencies, windows, equality checks, optional flag)
- `EqualityCheck` — equality-filter check with `is_unique` flag for codegen
- `FeasibleWindow` — STN-derived window bounds between two field accessors
- `JoinPlanError` — raised for invalid configurations
