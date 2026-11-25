# File statistics performance

Measured cost of `calculateStatsFor` and of each part of its scan, from `filestatistics_benchmark`; how to run it:
[testing.md](testing.md), "File statistics benchmark". Figures are milliseconds with a warm cache, the medians of two
runs of 9 samples each. The `mirror` cases replace one part of the scan each: the case names are explained in the
benchmark's source.

## Setup

| | Windows laptop |
|---|---|
| CPU | Core i3-2310M: 2 cores, 4 logical processors, not hybrid |
| Disk | SATA SSD (Samsung 750 EVO), NTFS |
| OS | Windows 11 23H2 |
| Toolchain | MSVC 2022, Qt 6.11.2, LTO |
| Commit | 30f95f09 |
| Threads | `calculateStatsFor`'s default: 4. Application pool: 2 |

## Large tree: 102k files in 2,221 folders

| One thread, every folder in turn | Run 1 | Run 2 |
|---|---|---|
| Listing alone | 402 | 391 |
| + a `CFileSystemObject` per entry | 492 | 464 |
| + the largest files | 502 | 485 |
| `calculateStatsFor`, 1 thread | 504 | 489 |

| 4 threads | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor` | 272 | 229 |
| Mirror of the current scan | 236 | 229 |
| P3: one lock per folder, wakeups only for queued folders | 241 | 228 |
| F3: no insertion of a file below the smallest of the largest files | 259 | 232 |
| F2: a `CFileSystemObject` only for a largest-file candidate | 215 | 201 |
| F2 without the largest files, as a folder size needs | 189 | 184 |
| P3 + F2 + F3 | 211 | 201 |

`calculateStatsFor` with 2 threads: 283 and 281.

## Narrow tree: a chain of 60 folders, 20 files each

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor`, 1 thread | 13.8 | 14.9 |
| `calculateStatsFor`, 4 threads | 15.2 | 16.5 |
| Mirror of the current scan | 15.3 | 16.9 |
| P3 | 14.0 | 13.7 |
| F1: the calling thread scans, helper threads start only for queued folders | 13.1 | 13.1 |

## Every folder's size: 1,000 folders of 5 files, each a task on the application pool

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor`, 4 threads | 600 | 597 |
| `calculateStatsFor`, 2 threads | 398 | 399 |
| `calculateStatsFor`, 1 thread | 327 | 327 |
| Mirror of the current scan | 591 | 587 |
| P3 | 594 | 593 |
| F1 | 140 | 135 |
| Starting and joining 4 threads per folder, without scanning | 451 | 433 |

## Findings

- Listing is about 80% of a one-thread scan of the large tree; building a `CFileSystemObject` per entry 15-19%, and the
  largest files 2-4%. The queue's locking and wakeups cost nothing measurable there.
- Starting and joining `calculateStatsFor`'s threads takes about 0.2 ms per thread, more than scanning a small folder.
- P3 and F3 gain nothing measurable on the large tree. P3 gains 1-3 ms on the narrow tree.
