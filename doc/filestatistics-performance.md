# File statistics performance

Measured cost of `calculateStatsFor` and of each part of its scan, from `filestatistics_benchmark`; how to run it:
[testing.md](testing.md), "File statistics benchmark". Figures are milliseconds with a warm cache, the medians of two
runs of 9 samples each. The `mirror` cases replace one part of the scan each: the case names are explained in the
benchmark's source. "Own threads" is the scan before it ran on the statistics pool: 4 threads started by every call.

## Setup

| | Windows laptop |
|---|---|
| CPU | Core i3-2310M: 2 cores, 4 logical processors, not hybrid |
| Disk | SATA SSD (Samsung 750 EVO), NTFS |
| OS | Windows 11 23H2 |
| Toolchain | MSVC 2022, Qt 6.11.2, LTO |
| Commit | 30f95f09 with the statistics pool |
| Threads | Statistics pool: 4 workers, so 5 threads per scan with the calling thread |

## Large tree: 102k files in 2,221 folders

| One thread, every folder in turn | Run 1 | Run 2 |
|---|---|---|
| Listing alone | 399 | 395 |
| + a `CFileSystemObject` per entry | 475 | 477 |
| + the largest files | 481 | 482 |

| `calculateStatsFor` | Run 1 | Run 2 |
|---|---|---|
| Pool of 1 worker: 2 threads | 275 | 267 |
| Pool of 2 workers: 3 threads | 251 | 252 |
| Pool of 4 workers: 5 threads | 240 | 242 |

| Statistics pool | Run 1 | Run 2 |
|---|---|---|
| Own threads | 246 | 246 |
| Mirror of the current scan | 248 | 245 |
| P3: one lock per folder, wakeups only for queued folders | 256 | 249 |
| F3: no insertion of a file below the smallest of the largest files | 270 | 247 |
| F2: a `CFileSystemObject` only for a largest-file candidate | 218 | 216 |
| F2 without the largest files, as a folder size needs | 189 | 187 |
| P3 + F2 + F3 | 209 | 216 |

## Narrow tree: a chain of 60 folders, 20 files each

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor`, pool of 1 worker | 13.2 | 13.0 |
| `calculateStatsFor`, pool of 4 workers | 14.0 | 14.6 |
| Own threads | 15.6 | 14.9 |
| Mirror of the current scan | 14.1 | 14.2 |
| P3 | 13.7 | 12.5 |

## Every folder's size: 1,000 folders of 5 files, each a task on the statistics pool

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor` | 124 | 137 |
| Own threads | 602 | 616 |
| Mirror of the current scan | 124 | 120 |
| P3 | 124 | 126 |

## Findings

- Listing is 82-83% of a one-thread scan of the large tree; building a `CFileSystemObject` per entry 16-17%, and the
  largest files 1%. The queue's locking and wakeups cost nothing measurable there.
- Sizing many small folders on the statistics pool is 4.5-5x faster than with own threads, whose start costs more than
  scanning a small folder. The large and narrow trees take the same time either way.
- F2 saves 12% on the large tree, and 24% without the largest files.
- P3 and F3 gain nothing measurable on the large tree. P3 gains up to 2 ms on the narrow tree.
