# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-07-23

## Latest Results (`e47eaee`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 38,537 | +31.5% ↑ |
| HNSW QPS (12T) | 108,905 | +34.0% ↑ |
| IVF QPS (1T) | 5,151 | +1.5% ↑ |
| IVF QPS (12T) | 13,485 | +18.4% ↑ |
| HNSW Insert/sec | 1,873 | +11.0% ↑ |
| IVF Insert/sec | 77,904 | +21.1% ↑ |
| Recall@100 | 86.9% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee"]
  y-axis "QPS" 0 --> 45000
  line [21803, 29297, 38537]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee"]
  y-axis "QPS" 0 --> 126000
  line [76009, 81296, 108905]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee"]
  y-axis "QPS" 0 --> 6000
  line [3457, 5074, 5151]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee"]
  y-axis "QPS" 0 --> 21000
  line [18039, 11386, 13485]
```

## Quick Trends

```
         HNSW QPS (1T)        38,537  ▁▄█
        HNSW QPS (12T)       108,905  ▁▂█
          IVF QPS (1T)         5,151  ▁██
         IVF QPS (12T)        13,485  █▁▃
       HNSW Insert/sec         1,873  ▁▆█
        IVF Insert/sec        77,904  ▁▄█
            Recall@100         86.9%  ▁▅█
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
