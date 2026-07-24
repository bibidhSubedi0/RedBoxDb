# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-07-24

## Latest Results (`4f25193`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 33,923 | -12.0% ↓ |
| HNSW QPS (12T) | 87,655 | -19.5% ↓ |
| IVF QPS (1T) | 5,312 | +3.1% ↑ |
| IVF QPS (12T) | 11,521 | -14.6% ↓ |
| HNSW Insert/sec | 1,704 | -9.0% ↓ |
| IVF Insert/sec | 64,542 | -17.2% ↓ |
| Recall@100 | 86.6% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193"]
  y-axis "QPS" 0 --> 45000
  line [21803, 29297, 38537, 33923]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193"]
  y-axis "QPS" 0 --> 126000
  line [76009, 81296, 108905, 87655]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193"]
  y-axis "QPS" 0 --> 7000
  line [3457, 5074, 5151, 5312]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193"]
  y-axis "QPS" 0 --> 21000
  line [18039, 11386, 13485, 11521]
```

## Quick Trends

```
         HNSW QPS (1T)        33,923  ▁▄█▆
        HNSW QPS (12T)        87,655  ▁▂█▃
          IVF QPS (1T)         5,312  ▁▇██
         IVF QPS (12T)        11,521  █▁▃▁
       HNSW Insert/sec         1,704  ▁▆█▆
        IVF Insert/sec        64,542  ▁▄█▄
            Recall@100         86.6%  ▁▅█▇
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 4 | `4f25193` | 2026-07-24 | 33,923 | 87,655 | 5,312 | 11,521 | 1,704 | 64,542 | 86.6% |
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
