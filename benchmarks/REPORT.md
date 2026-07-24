# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-07-24

## Latest Results (`c02c154`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 67,622 | +75.5% ↑ |
| HNSW QPS (12T) | 164,953 | +51.5% ↑ |
| IVF QPS (1T) | 11,849 | +130.0% ↑ |
| IVF QPS (12T) | 29,081 | +115.7% ↑ |
| HNSW Insert/sec | 2,639 | +40.9% ↑ |
| IVF Insert/sec | 89,674 | +15.1% ↑ |
| Recall@100 | 86.6% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "c02c154"]
  y-axis "QPS" 0 --> 78000
  line [21803, 29297, 38537, 67622]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "c02c154"]
  y-axis "QPS" 0 --> 190000
  line [76009, 81296, 108905, 164953]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "c02c154"]
  y-axis "QPS" 0 --> 14000
  line [3457, 5074, 5151, 11849]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "c02c154"]
  y-axis "QPS" 0 --> 34000
  line [18039, 11386, 13485, 29081]
```

## Quick Trends

```
         HNSW QPS (1T)        67,622  ▁▂▃█
        HNSW QPS (12T)       164,953  ▁▁▃█
          IVF QPS (1T)        11,849  ▁▂▂█
         IVF QPS (12T)        29,081  ▄▁▁█
       HNSW Insert/sec         2,639  ▁▃▄█
        IVF Insert/sec        89,674  ▁▃▆█
            Recall@100         86.6%  ▁▅█▇
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 4 | `c02c154` | 2026-07-24 | 67,622 | 164,953 | 11,849 | 29,081 | 2,639 | 89,674 | 86.6% |
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
