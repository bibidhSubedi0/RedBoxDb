# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-07-24

## Latest Results (`9c90e4d`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 42,190 | +9.5% ↑ |
| HNSW QPS (12T) | 100,191 | -8.0% ↓ |
| IVF QPS (1T) | 5,818 | +12.9% ↑ |
| IVF QPS (12T) | 13,565 | → |
| HNSW Insert/sec | 1,973 | +5.3% ↑ |
| IVF Insert/sec | 64,329 | -17.4% ↓ |
| Recall@100 | 86.5% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "9c90e4d"]
  y-axis "QPS" 0 --> 49000
  line [21803, 29297, 38537, 42190]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "9c90e4d"]
  y-axis "QPS" 0 --> 126000
  line [76009, 81296, 108905, 100191]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "9c90e4d"]
  y-axis "QPS" 0 --> 7000
  line [3457, 5074, 5151, 5818]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "9c90e4d"]
  y-axis "QPS" 0 --> 21000
  line [18039, 11386, 13485, 13565]
```

## Quick Trends

```
         HNSW QPS (1T)        42,190  ▁▃▇█
        HNSW QPS (12T)       100,191  ▁▂█▆
          IVF QPS (1T)         5,818  ▁▆▆█
         IVF QPS (12T)        13,565  █▁▃▃
       HNSW Insert/sec         1,973  ▁▅▇█
        IVF Insert/sec        64,329  ▁▄█▄
            Recall@100         86.5%  ▁▅█▆
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 4 | `9c90e4d` | 2026-07-24 | 42,190 | 100,191 | 5,818 | 13,565 | 1,973 | 64,329 | 86.5% |
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
