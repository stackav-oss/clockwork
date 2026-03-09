# Clockwork Changelog

Log of notable changes in each Clockwork OSS release.

Clockwork follows a date-based year.month versioning scheme.

## [2026.3] - 2026-03-20

### Added

- **DFL (Declarative Functional Language)**: New pure-functional sublanguage for runtime computations
  - Expression syntax with arithmetic, comparison, and logical operators
  - Conditional expressions: `if-then-else`, `cond`, and `match` with pattern matching
  - User-defined template-style functions with optional type annotations
  - Variadic function parameters and spread operator
  - Built-in functions: `min`, `max`, `abs`, `clamp`, `sum`, `count`, `mean`, `any`, `all`, `map`, `filter`, `flatten`
  - Trait system for type checking of operators
  - Tuple/array literal syntax
  - See [DFL Reference](clockwork/docs/reference/dfl/README.md) for details
- **Struct-of-Arrays (SoA) Containers**: New `FixedSoa` and `VarSoa` container types for SoA data layout
  - Memory-efficient alternative to `FixedArray`/`VarArray` that eliminates padding overhead
  - `ElementRef` proxy provides AoS-style API while data is stored in SoA layout
  - Direct field array access via `view_<field>()` methods for vectorization
  - Full STL compatibility with iterators and algorithms
  - Transparent schema evolution between AoS and SoA layouts
  - See [SoA Containers Reference](clockwork/docs/reference/soa.md) for details
- **State and Config Snapshot/Restore**: New mechanism for capturing and restoring Clockwork cog state and configuration (OI-3946).
  - `TakeSnapshots` policy for periodic state snapshots with configurable intervals
  - `SnapshotOnce` policy for one-time configuration snapshots at startup
  - `FirstMessage` data source for loading state from logged snapshots
  - `SerializedDataFile` enhancements for state restoration from files
  - Support for fallback data sources when snapshots are unavailable
  - See [Clockwork State/Config Snapshot/Restore Guide](clockwork/docs/reference/state_snapshot_and_restore.md) for details
- **Unit Test Framework for Cogs**: Generated C++ cog unit-test wrappers for executing a cog in a unit test.
  - See [Cog Unit Tests Reference](clockwork/docs/reference/cog_unit_tests.md) for details.
- **Clockwork Observability Signals**: New support for generating scalar signals in a cog.
  - Automatically aggregated and logged.
  - Pre-aggregation and post-aggregation policies.
  - Signal metadata publication and extraction tooling.
  - See [Signals Reference](clockwork/docs/reference/signals.md) for details.
- **Boilerplate Reduction in Schemas**: Simplify the generation syntax and build rules for Clockwork entities.
  - New clk file syntax to specify code generation targets with attributes instead of defining them manually.
  - Bazel rules implicitly create required targets for each Clockwork entity.
  - Gazelle plugin to auto-generate build files from clk files.
- New `@clockwork::std` standard library module (holds DSL entities used in user code, such as policies)

### Changed

- Updated Clockwork schemas to use new generator format.
- Enhanced `LogicalSystemInterface` to support state restoration configuration in deterministic runner.
- Expanded offboard logging observability with OpenTelemetry tracing instrumentation support (OI-4168).

## [2025.9] - 2025-09-30

### Added

- Cog Metrics: Clockwork cogs now generate metrics related to their execution timing (OI-2851).
- Pinion Safety Margins: Static checks and runtime checks for pinion message buffers (OI-2998).
- Redundant logging options (OI-3294).
- New, faster, C++ log upgrader (OI-3030).
- [callsig](jewels/callsig/README.md): New call signature library to handle function arguments (OI-3240) and outcomes (OI-3024).
- Option to echo a fixed number of messages in `channel_spy` (OI-3348).
- Syntax to allow unconnected channels (OI-3598).

### Changed

- Exposed mmap_region used by `pinion` for re-use (OT-2386).
- Build system migrated to Bazel 8, various dependency updates (DX-2356).
- Toolchain updated to Clang 21 (DX-2796).
- Improvements to Python dependency handling (DX-2452).

### Fixed

- Python interpreter management in Python cogs (OI-3682).
- Error handling and reconnection handling in TCP Bridge (OI-3523).
