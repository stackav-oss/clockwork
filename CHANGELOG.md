# Clockwork Changelog

Log of notable changes in each Clockwork OSS release.

Clockwork follows a date-based year.month versioning scheme.

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
