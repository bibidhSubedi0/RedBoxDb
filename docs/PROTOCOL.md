# RedBoxDb Wire Protocol

This documents the raw binary TCP protocol RedBoxServer speaks, as
implemented in `src/server.cpp`. It supersedes the summary comment
previously at the bottom of that file (kept in sync, but this is the
complete reference — including two fields that comment omitted).

There is no HTTP, no JSON, no framing library — just a fixed-size
header followed by a command-specific payload, all little-endian.

## Request framing

Every request from client to server starts with the same 5-byte header:

| Offset | Field | Type     | Size    | Description                                   |
|--------|-------|----------|---------|------------------------------------------------|
| 0      | CMD   | `uint8`  | 1 byte  | Command opcode (see [Commands](#commands))     |
| 1      | META  | `uint32` | 4 bytes | Context-dependent (vector ID, count, length…)  |
| 5      | —     | bytes    | N bytes | Optional payload, meaning depends on CMD       |

The server reads exactly 5 bytes for the header, then reads whatever
payload that command requires (usually a fixed number of bytes derived
from the active database's dimension, or from a length META field).
There's no total-length prefix — the client and server must agree on
how many bytes a given command's payload is, which is why an active
database (for its `dim`) is required before most commands work at all.

## Connection lifecycle

A freshly-connected socket has no active database. The **first**
command must be `SELECT_DB` (opens or creates an IVF-indexed database)
or `CREATE_HNSW_DB` (opens or creates an HNSW-indexed database) — every
other command checks for an active database and the connection is
dropped if there isn't one yet (`if (!active_db) break;`).

Selecting a database that already exists on disk just opens it; the
`dim`/`capacity` (and, for HNSW, `M`/`ef_construction`) you pass are
only used if the database doesn't exist yet. If it does exist and your
requested `dim` doesn't match the file's actual dimension, the server
logs a warning server-side but still acks with `1` and lets you
proceed — mismatched-dimension inserts/searches will misbehave, this
is not currently rejected at the protocol level.

## Commands

`CMD ID` values are defined in `src/server.cpp`. All multi-byte
integers are little-endian.

### 1 — INSERT

Insert a vector with a caller-specified ID.

- META: vector ID (`uint32`, truncated — see [Notes](#notes-and-gotchas))
- Payload: `dim * 4` bytes, raw `float32` vector data
- Response: `1` byte, always `'1'`

### 2 — SEARCH

Find the single nearest vector by L2 distance.

- META: ignored
- Payload: `dim * 4` bytes, raw `float32` query vector
- Response: `4` bytes, `int32` result ID (`-1` if the database is empty)

### 3 — DELETE

Soft-delete a vector by ID (excluded from future searches; tombstoned,
not removed from disk immediately — see `docs/DEPLOYMENT.md`).

- META: vector ID (`uint32`)
- Payload: none
- Response: `1` byte, `'1'` if found and deleted, `'0'` otherwise

### 4 — SELECT_DB

Open (or create, if it doesn't exist) an IVF-indexed database and make
it the connection's active database.

- META: name length in bytes (`uint32` — see
  [Database name rules](#database-name-rules); a length over 64 drops
  the connection immediately, a length of 0 is read fine but then
  fails validation like any other invalid name)
- Payload: `<name bytes><dim: uint32><capacity: uint32>`
  — **the in-code protocol summary comment omits the trailing
  `dim`/`capacity` fields; they are required.**
- Response: `1` byte — `'0'` if the name fails validation (a
  zero-length name always fails; see
  [Database name rules](#database-name-rules) — connection stays open,
  you can retry); `'1'` on success. If `name_len` exceeds 64, the
  server doesn't respond at all and drops the connection.

### 5 — UPDATE

Overwrite an existing vector's data in place by ID.

- META: vector ID (`uint32`)
- Payload: `dim * 4` bytes, raw `float32` vector data
- Response: `1` byte, `'1'` if the ID exists (and isn't deleted),
  `'0'` otherwise

### 6 — INSERT_AUTO

Insert a vector with a server-assigned, auto-incrementing ID.

- META: ignored
- Payload: `dim * 4` bytes, raw `float32` vector data
- Response: `8` bytes, `uint64` assigned ID

### 7 — SEARCH_N

Find the N nearest vectors, closest first.

- META: N (`uint32`, cast to `int` server-side; `N <= 0` is accepted
  and just returns zero results rather than erroring)
- Payload: `dim * 4` bytes, raw `float32` query vector
- Response: `<count: uint32><ids: int32 * count>` — `count` may be `0`
  with no IDs following

### 8 — DROP_DB

Drop the connection's active database entirely: removes it from the
in-memory catalog, deletes `<name>.db` and `<name>.db.del` from disk.
The connection has no active database afterward.

- META: ignored
- Payload: none
- Response: `1` byte, `'1'` on success, `'0'` if there was no active
  database to identify (shouldn't normally happen given the lifecycle
  rule above)

### 9 — SET_PROBES

Set the number of IVF clusters to probe during search. No-op (but
still acks) on an HNSW database, or if `probes` is `0`.

- META: probe count (`uint32`, cast to `uint8` server-side — valid
  range is really 1–255)
- Payload: none
- Response: `1` byte, always `'1'`

### 10 — CREATE_HNSW_DB

Open (or create) an HNSW-indexed database and make it the connection's
active database.

- META: name length in bytes (`uint32`, same 64-byte limit as
  `SELECT_DB`)
- Payload: `<name bytes><dim: uint32><capacity: uint32><M: uint8><ef_construction: uint16>`
- Response: `1` byte — same `'0'`/`'1'`/drop-connection behavior as
  `SELECT_DB` for invalid names; no explicit ack byte is sent for the
  `name_len > 64` case (connection is dropped)

### 11 — SET_HNSW_EF

Set the `ef_search` parameter (search-time candidate list size) for
HNSW queries against the active database.

- META: ef value (`uint32`, cast to `uint16` server-side)
- Payload: none
- Response: `1` byte, always `'1'`

### 12 — LIST_DBS

List every database known to the server's metadata store.

**Requires the server to be built with PostgreSQL support**: the
`REDBOX_ENABLE_PG` CMake option (`ON` by default, auto-disables if
`find_package(PostgreSQL)` doesn't find it) compiles in a metadata
store backed by libpqxx, defining `REDBOX_PG_ENABLED` and connecting
via `REDBOX_PG_*` environment variables (see `.github/workflows/ci.yml`
for the exact variable names). Without PG support compiled in and
configured, this command always responds with a count of `0` rather
than erroring.

- META: ignored
- Payload: none
- Response: `<count: uint32>`, followed by `count` entries of:
  `<name_len: uint8><name: name_len bytes><dimensions: uint32><index_type: uint8><vector_count: uint64>`
  — `index_type` is `0` for IVF, `1` for HNSW (`CoreEngine::IndexType`)

### 13 — DB_INFO

Get detailed stats for the connection's active database.

**Also requires `REDBOX_PG_ENABLED`.**

- META: ignored
- Payload: none
- Response: `1` byte `ok` flag, then — only if `ok == 1` —
  `<vector_count: uint64><capacity: uint64><next_id: uint64><index_type: uint8><dim: uint32>`.
  `ok` is `0` (with no further bytes sent) if there's no active
  database, the server wasn't built with PG support, or PG isn't
  configured/reachable.

## Database name rules

Applies to both `SELECT_DB` and `CREATE_HNSW_DB`:

- 1–64 bytes long (`0 < len <= 64`)
- ASCII alphanumeric, `_`, or `-` only — anything else (including
  non-ASCII/UTF-8 multi-byte sequences) is rejected

A name that's too long causes the server to close the connection
without responding. A name that's the right length but fails character
validation gets an explicit `'0'` response and the connection stays
open.

## Notes and gotchas

- **The META field is `uint32` on the wire, but IDs are `uint64`
  internally** — `INSERT_AUTO`'s response is a full 8-byte ID, and
  `RedBoxVector::insert()` takes a `uint64_t id`, but `INSERT`/`DELETE`/
  `UPDATE` can only ever *address* a vector via the 4-byte META field.
  There is no way to `INSERT` (with a caller-chosen ID) or
  `DELETE`/`UPDATE` a vector whose ID is above `2^32 - 1` — the ID
  space these three commands can reach is narrower than the ID space
  `INSERT_AUTO` can produce. What a given client does when asked to
  encode a too-large ID depends on the client (`Client/client.py`'s
  `struct.pack('<BI', ...)` raises `struct.error` rather than
  truncating silently, since Python's `struct` module rejects
  out-of-range values for `'I'`; a client written in a language that
  does a raw narrowing cast instead could truncate silently). Tracked
  as a protocol-level limitation in #88; documenting the wire-format
  fact here so it's not a surprise regardless of how #88 gets resolved.
- **No request-response correlation.** Responses are read strictly in
  the order requests were sent on a single connection — there's no
  request ID to match a response back to a specific call. Concurrent
  requests on one connection are not safe; use one connection per
  concurrent caller (or hold a lock around request/response pairs, as
  `Client/client.py` implicitly does by being synchronous).
- **`Client/client.py` is the reference implementation** of this
  protocol from the client side — see `_handshake()`,
  `_handshake_hnsw()`, and the per-command methods for exact
  `struct.pack` layouts matching everything above.
