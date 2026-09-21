# Aligner Compiler — Developer Documentation

The `clockwork/dsl/aligner/` package implements the compiler passes for Clockwork aligners.
Aligners are declarative specifications that describe how to synchronize multiple data streams by expressing timing constraints and optimization objectives.

## Compiler Pipeline

The aligner compiler processes `.clk` aligner definitions through a multi-phase pipeline.

- **Parse & Normalize** — Extract inputs, constraints, objectives, and conditionals from the DSL spec.
- **Simple Temporal Network (STN) Construction** — Build a weighted directed graph from the extracted constraints.
- **Run Floyd-Warshall on STN** — Compute tightest bounds and check consistency.
- **Objective Analysis** — Classify search type per input (LOOP vs LOOKUP).
- **Join Plan Computation** — Greedy level-by-level ordering with per-level objective classification.
- **Code Generation** — Emit runtime C++ / Python.

The "Parse & Normalize" phase encompasses parsing, type checking, and spec extraction:

- **Parsing** — Builds the `Aligner` IR node from `.clk` source (handled by the general Clockwork parser and `Aligner.resolve()`).
- **Type checking** — Validates body statements, expands function calls, and stores `expanded_expr` on each spec statement. See `../type_check.py`.
- **Spec extraction** — Normalizes expanded expressions into structured difference constraints and objectives. See `../extract_specs.py` and the [spec extraction guide](spec_extraction.md).

## Unified Pipeline Entry Point

The `analyze_aligner()` function in `pipeline.py` chains all analysis stages into a single call.
It builds one all-present representative join plan for code generation and computes [maximally permissive windows](maximally_permissive.md) for codegen's initial search windows.
Results are stored on the `Aligner` IR node as `aligner.analysis`.

The pipeline is automatically invoked during compilation by `clkc.py`.
It runs between IR compilation and code generation.
After `compiler.compile_source_file()` resolves the module, `clkc` type-checks
each aligner and runs `analyze_aligner()`.
This makes analysis results available to `_render_aligner_impl()` during codegen.

## Code Generation

The `clockwork/dsl/aligner/gen/` package handles aligner C++ code generation.
For each aligner, the compiler constructs a synthetic `cog.Cog` IR node and feeds it through the existing `CppCog` / `CppDial` rendering pipeline.
The generated cog runs as a `SimpleCog<AlignerPolicy>`.

## Documentation Index

- [Spec Extraction](spec_extraction.md) — How constraint and objective extraction works.
- [STN Analysis](stn_analysis.md) — How STN construction and Floyd-Warshall work.
- [Objective Analysis](objective_analysis.md) — How objective decomposition and search type classification work.
- [Join Plan](join_plan.md) — How join plan computation and level ordering work.
- [Maximally Permissive Windows](maximally_permissive.md) — How initial search windows are computed across `has_candidates` branch combinations.
- `pipeline.py` — Unified analysis entry point (`analyze_aligner()`).
- Code generation — See `clockwork/dsl/aligner/gen/` package.

## See also

These are internal developer docs.
We also have [user-facing aligner documentation](../../../docs/reference/aligners.md) that describes the aligner concept, syntax, semantics, and examples for users of the aligner system.
