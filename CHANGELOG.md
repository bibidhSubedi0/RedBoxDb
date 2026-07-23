# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Versions below track the `redboxdb` PyPI package. The core C++ engine is
versioned and released together with the SDK.

## [Unreleased]

### Added
- HNSW graph index with mmap persistence, as an alternative to linear scan
  and IVF clustering for approximate nearest-neighbor search.
- Runtime IVF probe count control via a new server command.
- Linux support (previously Windows x64 only); CI now builds and tests on
  both Windows and Ubuntu.

### Fixed
- Server no longer crashes on a buffer overflow path in HNSW's
  `search_layer_1`.
- `db_name` is now sanitized to prevent path traversal and unbounded memory
  allocation from a malicious or malformed name.
- Server sends an explicit error response for unknown protocol commands
  instead of leaving the client connection desynchronized.
- Fixed thread-safety issues, a buffer overflow, and MinGW TLS crashes
  surfaced by the Linux/MinGW build.

## [1.0.6] - 2026-05-27

### Added
- IVF (Inverted File) clustering with K-Means++ initialization for
  sub-millisecond approximate search on larger datasets.
- Client can request the database capacity at initialization.
- `delete_db` support from the Python client.
- Columnar memory layout for vector storage.
- Search scan is now parallelized across all available CPU cores.

### Changed
- Migrated the C++ toolchain from MSVC to GCC, and CI accordingly.

## [1.0.5] - 2026-03-04

### Added
- Multithreaded server: concurrent client connections are now handled in
  parallel instead of serially.
- Indexing support for vector updates.
- Auto-incrementing IDs, with corresponding client-side improvements.

### Fixed
- Tombstone (`.del` file) compaction: stale deleted-entry records are now
  rewritten atomically once they exceed a slack threshold, instead of the
  file growing unbounded.
- Fixed `.del` file rename on Windows, where `std::rename` does not
  overwrite an existing file (needed for tombstone compaction to work
  cross-platform).

## [1.0.4] - 2026-02-20

### Added
- AVX2 SIMD-accelerated distance calculation, with a scalar fallback for
  CPUs without AVX2 support.

### Changed
- Updated ingestion throughput metrics in the README to reflect the SIMD
  distance calculation improvements.

## [1.0.3] - 2026-01-15

### Fixed
- Fixed a failed PyPI release.

## [1.0.2] - 2026-01-15

### Added
- `pip install redboxdb` distribution: the Python package now bundles the
  compiled C++ server binary, built by CI.
- Initial `README.md`.

## [1.0.1] - 2026-01-15

### Fixed
- Fixed a release issue from `1.0.0`.

## [1.0.0] - 2026-01-15

### Added
- Initial release of the `redboxdb` Python SDK and C++ vector database
  engine: insert, search (L2 distance), update, and delete over a custom
  binary TCP protocol.

[Unreleased]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.6...HEAD
[1.0.6]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.5...v1.0.6
[1.0.5]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.4...v1.0.5
[1.0.4]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/bibidhSubedi0/RedBoxDb/compare/v1.0...v1.0.1
[1.0.0]: https://github.com/bibidhSubedi0/RedBoxDb/releases/tag/v1.0
