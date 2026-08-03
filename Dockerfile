# syntax=docker/dockerfile:1
#
# Multi-stage build for RedBoxServer.
#
# Build:  docker build -t redboxdb .
# Run:    docker run -p 8080:8080 -v redboxdb-data:/data redboxdb
#
# PostgreSQL metadata store (optional, enabled by default -- see
# docs/DEPLOYMENT.md and docs/PROTOCOL.md's LIST_DBS/DB_INFO commands):
#   docker run -p 8080:8080 -v redboxdb-data:/data \
#     -e REDBOX_PG_HOST=host.docker.internal -e REDBOX_PG_PORT=5432 \
#     -e REDBOX_PG_DBNAME=redbox -e REDBOX_PG_USER=redbox \
#     -e REDBOX_PG_PASSWORD=secret redboxdb
# Without REDBOX_PG_HOST set, the server runs fine with PostgreSQL features
# inactive (LIST_DBS/DB_INFO just report unavailable, per docs/PROTOCOL.md).

# ---------------------------------------------------------------------------
# Build stage
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        ca-certificates \
        libpq-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Matches .github/workflows/ci.yml's Linux build exactly (Ninja, Release,
# g++, REDBOX_ENABLE_PG on by default). FetchContent pulls spdlog,
# googletest, and libpqxx at configure time, so this needs network access.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
    && cmake --build build --target RedBoxServer -j"$(nproc)"

# ---------------------------------------------------------------------------
# Runtime stage
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        libpq5 \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /data --shell /usr/sbin/nologin redboxdb

WORKDIR /data

# RedBoxServer resolves "sql/schema.sql" (for the optional PostgreSQL
# metadata store's migrations) relative to its current working directory,
# not relative to the binary -- see Store::run_migrations() in
# src/metadata_store.cpp. It needs to actually be at <WORKDIR>/sql/schema.sql
# for REDBOX_ENABLE_PG builds to run migrations successfully on startup.
COPY --from=build /src/build/src/RedBoxServer /usr/local/bin/RedBoxServer
COPY --from=build /src/sql ./sql

# .db / .db.del files are also written relative to the working directory
# (see docs/DEPLOYMENT.md's backup section), so this is the one directory
# that needs to persist across restarts.
RUN chown -R redboxdb:redboxdb /data
USER redboxdb

EXPOSE 8080
VOLUME ["/data"]

ENTRYPOINT ["/usr/local/bin/RedBoxServer"]
