# RedBoxDb Performance Dashboard

> Auto-generated on every commit to main. Last updated: 2026-08-03

## Latest Results (`5feacf8`)

| Metric | Value | vs Previous |
|--------|-------|-------------|
| HNSW QPS (1T) | 47,040 | +102.5% ↑ |
| HNSW QPS (12T) | 131,066 | +119.0% ↑ |
| IVF QPS (1T) | 6,996 | +58.0% ↑ |
| IVF QPS (12T) | 16,658 | +65.6% ↑ |
| HNSW Insert/sec | 2,516 | +31.6% ↑ |
| IVF Insert/sec | 97,904 | +57.4% ↑ |
| Recall@100 | 86.8% | → |

## HNSW 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193", "9d81807", "5feacf8"]
  y-axis "QPS" 0 --> 55000
  line [21803, 29297, 38537, 33923, 23225, 47040]
```

## HNSW 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "HNSW 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193", "9d81807", "5feacf8"]
  y-axis "QPS" 0 --> 151000
  line [76009, 81296, 108905, 87655, 59834, 131066]
```

## IVF 1-NN QPS (1 thread)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (1 thread)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193", "9d81807", "5feacf8"]
  y-axis "QPS" 0 --> 9000
  line [3457, 5074, 5151, 5312, 4427, 6996]
```

## IVF 1-NN QPS (12 threads)

```mermaid
xychart-beta
  title "IVF 1-NN QPS (12 threads)"
  x-axis ["0ec05f3", "5366613", "e47eaee", "4f25193", "9d81807", "5feacf8"]
  y-axis "QPS" 0 --> 21000
  line [18039, 11386, 13485, 11521, 10058, 16658]
```

## Quick Trends

```
         HNSW QPS (1T)        47,040  ▁▃▆▄▁█
        HNSW QPS (12T)       131,066  ▂▃▆▄▁█
          IVF QPS (1T)         6,996  ▁▄▄▅▃█
         IVF QPS (12T)        16,658  █▂▄▂▁▇
       HNSW Insert/sec         2,516  ▁▃▅▃▅█
        IVF Insert/sec        97,904  ▁▂▅▂▂█
            Recall@100         86.8%  ▁▅█▇▇█
```

## Full History

| # | Commit | Date | HNSW 1T | HNSW NT | IVF 1T | IVF NT | HNSW Ins | IVF Ins | Recall |
|---|--------|------|---------|---------|--------|--------|----------|---------|--------|
| 6 | `5feacf8` | 2026-08-03 | 47,040 | 131,066 | 6,996 | 16,658 | 2,516 | 97,904 | 86.8% |
| 5 | `9d81807` | 2026-07-26 | 23,225 | 59,834 | 4,427 | 10,058 | 1,912 | 62,217 | 86.7% |
| 4 | `4f25193` | 2026-07-24 | 33,923 | 87,655 | 5,312 | 11,521 | 1,704 | 64,542 | 86.6% |
| 3 | `e47eaee` | 2026-07-23 | 38,537 | 108,905 | 5,151 | 13,485 | 1,873 | 77,904 | 86.9% |
| 2 | `5366613` | 2026-07-22 | 29,297 | 81,296 | 5,074 | 11,386 | 1,687 | 64,329 | 86.3% |
| 1 | `0ec05f3` | 2026-07-21 | 21,803 | 76,009 | 3,457 | 18,039 | 1,219 | 54,340 | 85.7% |
