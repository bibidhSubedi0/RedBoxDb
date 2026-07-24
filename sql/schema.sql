-- RedBoxDb PostgreSQL Metadata Schema
-- Run once on first startup to create tables.

DO $$ BEGIN
    CREATE TYPE index_type AS ENUM ('IVF', 'HNSW');
EXCEPTION
    WHEN duplicate_object THEN NULL;
END $$;

CREATE TABLE IF NOT EXISTS databases (
    id                    SERIAL PRIMARY KEY,
    name                  VARCHAR(64) UNIQUE NOT NULL,
    dimensions            INTEGER NOT NULL,
    index_type            index_type NOT NULL DEFAULT 'IVF',
    capacity              BIGINT NOT NULL,
    vector_count          BIGINT NOT NULL DEFAULT 0,
    next_id               BIGINT NOT NULL DEFAULT 1,
    hnsw_m                SMALLINT,
    hnsw_ef_construction  SMALLINT,
    hnsw_ef_search        SMALLINT,
    hnsw_max_level        SMALLINT,
    hnsw_entry_point      INTEGER DEFAULT 0,
    hnsw_graph_version    INTEGER DEFAULT 0,
    ivf_num_clusters      SMALLINT,
    ivf_num_probes        SMALLINT,
    ivf_initialized       BOOLEAN DEFAULT FALSE,
    created_at            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at            TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS audit_log (
    id          BIGSERIAL PRIMARY KEY,
    db_name     VARCHAR(64) REFERENCES databases(name) ON DELETE CASCADE,
    operation   VARCHAR(16) NOT NULL,
    vector_id   BIGINT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_audit_db_time ON audit_log(db_name, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_audit_time ON audit_log(created_at DESC);

CREATE TABLE IF NOT EXISTS server_config (
    key         VARCHAR(64) PRIMARY KEY,
    value       TEXT NOT NULL,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
