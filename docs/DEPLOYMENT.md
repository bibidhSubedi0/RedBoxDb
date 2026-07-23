# Deployment Guide

This document covers running `RedBoxServer` in production: system
requirements, OS tuning, running it as a service, backups, monitoring, and
capacity planning. It reflects the behavior of the server as implemented in
`src/server.cpp` and `src/engine.cpp` at the time of writing, not generic
advice — see the notes on current limitations below before you rely on any
of this for data you care about.

## System requirements

- **OS**: Linux or Windows x64. The `redboxdb` PyPI package currently
  bundles a prebuilt **Windows x64** binary only (see `setup.py`'s
  `package_data`); on Linux (or any other platform) you need to build the
  server from source — see "Building from Source" in the [README](../README.md).
- **Build toolchain** (source builds only): CMake 3.23+, a C++17 compiler
  (GCC, Clang, or MSVC).
- **Memory**: the server memory-maps each open database file in full
  (`mmap`/`MapViewOfFile`), so resident memory scales with the sum of your
  databases' allocated capacity, not just the number of vectors actually
  inserted — see [Capacity planning](#capacity-planning).

## Network

The server listens on **TCP port 8080** on all interfaces
(`INADDR_ANY`), hardcoded via `const int PORT = 8080` in `src/server.cpp`.
There is currently no CLI flag or environment variable to change it —
changing the port means editing that constant and rebuilding. If you need
a different externally-facing port or TLS termination, put a TCP proxy in
front of the server rather than exposing it directly.

The protocol is a custom raw binary format over TCP (see the comment block
at the bottom of `src/server.cpp`), not HTTP — so it will not pass through
an HTTP reverse proxy or load balancer that expects HTTP semantics. Use a
plain TCP proxy (e.g. `socat`, `haproxy` in TCP mode, or an L4 load
balancer) if you need one.

## OS tuning

The server uses a **thread-per-connection** model: every accepted client
gets its own detached `std::thread` for the lifetime of the connection
(`src/server.cpp`, the `accept()` loop in `main()`), with no cap on
concurrent connections or thread pool. Each connection holds one socket
file descriptor and one OS thread for as long as the client stays
connected.

- **Open file descriptors**: raise the process's `nofile` limit if you
  expect many concurrent client connections, since each one consumes a
  file descriptor (plus one file descriptor per open database's mmap'd
  file). As a starting point for a server expecting on the order of a few
  thousand concurrent connections:
  ```bash
  ulimit -n 65536
  ```
  Under systemd, set this via `LimitNOFILE=` in the unit file (see below)
  rather than the shell's `ulimit`.
- **Max user processes/threads**: since every connection spawns an OS
  thread, also raise `nproc`/`RLIMIT_NPROC` if you expect high connection
  concurrency, or put a connection limit in front of the server (e.g. at
  a TCP proxy) if you'd rather bound thread growth than raise the limit
  indefinitely.
- **mmap count**: each open database is a single `mmap` region, so
  `vm.max_map_count` (Linux) is only a concern if you plan to open a very
  large number of distinct databases concurrently in one server process
  — the default (`65530` on most distros) is enough for typical usage.

## Capacity planning

Each database's on-disk file is **preallocated to a fixed size at creation
time** and is not resized afterward. When a client selects or creates a
database (`CMD_SELECT_DB` / `CMD_CREATE_HNSW_DB`), it sends a
`dimensions` and `capacity` — the server allocates
`header + index-specific blocks sized for that capacity` (see the layout
comments in `include/redboxdb/storage_manager.hpp`) as one mmap'd file of
that fixed size. There is no dynamic growth: once a database's `capacity`
slots are filled, further inserts against it are rejected
(`if (slot >= _manager->get_header()->max_capacity)` in `src/engine.cpp`).

**Plan capacity upfront, per database.** Sizing a database larger than you
need costs disk and mmap'd address space; sizing it too small means you'll
need to create a new, larger database and re-migrate the data (there's no
"alter capacity" operation) once you hit the ceiling.

Rough size-on-disk for an IVF database: `128 bytes header + capacity * dim
* 4 bytes (float_block) + capacity * 2 bytes (cluster_block) + capacity *
8 bytes (id_block) + num_clusters * dim * 4 bytes (centroids)`. For an
HNSW database, add `capacity * 1 byte (level_block) + capacity *
edges_per_node(M) * 4 bytes (edge_block)`. Use this to estimate disk and
resident-memory footprint before choosing a capacity.

## Running as a systemd service

There's no packaged unit file in the repo, so here's a working baseline —
adjust `User`, `WorkingDirectory`, and paths for your setup. Database files
are created relative to the process's working directory (see the
`db_name + ".db"` construction in `src/server.cpp`), so `WorkingDirectory`
determines where they end up.

```ini
# /etc/systemd/system/redboxdb.service
[Unit]
Description=RedBoxDb vector database server
After=network.target

[Service]
Type=simple
User=redboxdb
Group=redboxdb
WorkingDirectory=/var/lib/redboxdb
ExecStart=/usr/local/bin/RedBoxServer
Restart=on-failure
RestartSec=2
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

```bash
sudo mkdir -p /var/lib/redboxdb
sudo useradd --system --no-create-home redboxdb
sudo chown redboxdb:redboxdb /var/lib/redboxdb
sudo systemctl daemon-reload
sudo systemctl enable --now redboxdb
```

**Known limitation — no graceful shutdown handler.** `src/server.cpp`'s
`main()` only installs a handler for `SIGPIPE` (to ignore it); there is no
`SIGTERM`/`SIGINT` handler. `Manager::~Manager()` is the code path that
explicitly flushes mmap'd data to disk (`msync(..., MS_SYNC)` on POSIX,
`FlushViewOfFile` on Windows) — see `src/engine.cpp` — but that destructor
only runs today when a database is dropped via `CMD_DROP_DB`, not on
process shutdown, since the server has no code path that returns
normally from `main()` or otherwise unwinds the stack on `SIGTERM`. In
practice this means `systemctl stop` (which sends `SIGTERM`) terminates
the process via the OS's default disposition without an explicit sync;
you're relying on the OS's own dirty-page writeback (Linux flushes dirty
mmap'd pages in the background well within `vm.dirty_expire_centisecs`,
default ~30s) rather than a guaranteed immediate flush on stop. This is
worth being aware of for the backup guidance below, and would be a good
follow-up fix upstream (a signal handler that breaks the accept loop and
lets the `SharedState` — and its `Manager`s — destruct normally).

## Backup procedures

Each database is exactly two files in the working directory:
`<db_name>.db` (the mmap'd data) and `<db_name>.db.del` (the tombstone log
for soft-deletes, compacted periodically — see `compact_tombstones()` in
`src/engine.cpp`). Back up both together; a `.db` file without its
matching `.db.del` will still work but will show soft-deleted vectors as
present again.

Given the shutdown limitation above, the safest backup procedure with the
server as it exists today is:

1. Stop accepting new writes (stop the client workload, or stop the
   service).
2. Run `sync` (Linux) to flush the kernel's dirty page cache to disk.
3. Wait a couple of seconds, then copy the `*.db` and `*.db.del` files.

For online (no-downtime) backups, avoid copying the `.db` files directly
while the server is under write load — copying a live mmap'd file while
it's being written can capture a torn/inconsistent snapshot. If you need
zero-downtime backups, take them from a filesystem-level snapshot
(LVM snapshot, ZFS snapshot, or equivalent) instead of a plain file copy,
so the snapshot is atomic with respect to concurrent writes.

## Monitoring

There's no HTTP metrics or health-check endpoint — the protocol is raw
binary TCP only. Practical options with the server as it exists today:

- **Process supervision**: `systemctl status redboxdb` /
  `Restart=on-failure` (set above) covers crash recovery.
- **Liveness / readiness**: a plain TCP connect to port 8080 is enough to
  confirm the server accepted the socket and is listening — this is the
  same check the project's own CI uses to wait for the server to be ready
  before running verification (`.github/workflows/ci.yml`):
  ```bash
  nc -z 127.0.0.1 8080 && echo "up"
  ```
- **Logs**: the server logs via `spdlog` (see `include/redboxdb/logger.hpp`)
  with no file sink configured, so log output goes to stdout/stderr. Under
  systemd this is captured into the journal automatically —
  `journalctl -u redboxdb -f` to tail it.
- **Disk usage**: since each database is preallocated to a fixed capacity
  at creation (see [Capacity planning](#capacity-planning)), disk usage
  per database is predictable and known at creation time — monitor free
  disk space against the sum of your databases' allocated (not just
  used) capacity, since that's the space actually reserved on disk.
