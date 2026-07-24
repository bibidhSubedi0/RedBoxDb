# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-07-24

## Latest Results (`3d2d7fb`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 34,883 | -9.5% ↓ |
| HNSW QPS (12T) | 99,293 | -8.8% ↓ |
| IVF QPS (1T) | 5,462 | +6.0% ↑ |
| IVF QPS (12T) | 12,674 | -6.0% ↓ |
| HNSW Insert/sec | 2,012 | +7.4% ↑ |
| IVF Insert/sec | 65,127 | -16.4% ↓ |
| Recall@100 | 86.4% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "3d2d7fb"]
  y-axis "QPS" 0 --> 45000
  line [21803, 29297, 38537, 34883]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "3d2d7fb"]
  y-axis "QPS" 0 --> 126000
  line [76009, 81296, 108905, 99293]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "3d2d7fb"]
  y-axis "QPS" 0 --> 7000
  line [3457, 5074, 5151, 5462]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "3d2d7fb"]
  y-axis "QPS" 0 --> 21000
  line [18039, 11386, 13485, 12674]
```

## Quick Trends

```
         HNSW QPS (1T)        34,883  ▁▄█▇
        HNSW QPS (12T)        99,293  ▁▂█▆
          IVF QPS (1T)         5,462  ▁▇▇█
         IVF QPS (12T)        12,674  █▁▃▂
       HNSW Insert/sec         2,012  ▁▅▇█
        IVF Insert/sec        65,127  ▁▄█▄
            Recall@100         86.4%  ▁▅█▆
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 4 | `3d2d7fb` | 2026-07-24 | 34,883 | 99,293 | 5,462 | 12,674 | 2,012 | 65,127 | 86.4% |
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
