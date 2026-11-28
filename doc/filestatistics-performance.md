# File statistics performance

Measured cost of `calculateStatsFor` and of each part of its scan, from `filestatistics_benchmark`; how to run it:
[testing.md](testing.md), "File statistics benchmark". Figures are milliseconds with a warm cache, the medians of two
runs of 9 samples each. The `mirror` cases replace one part of the scan each: the case names are explained in the
benchmark's source.
- "Own threads" starts 4 threads on every call, as the scan did before it ran on the statistics pool.
- "Object per entry" builds a `CFileSystemObject` for every entry, as the scan did before `FileSystemEntryView`.

## Setup

| | Windows laptop |
|---|---|
| CPU | Core i3-2310M: 2 cores, 4 logical processors, not hybrid |
| Disk | SATA SSD (Samsung 750 EVO), NTFS |
| OS | Windows 11 23H2 |
| Toolchain | MSVC 2022, Qt 6.11.2, LTO |
| Commit | 1cedba4e with `FileSystemEntryView` and `largestFilesCount` |
| Threads | Statistics pool: 4 workers, so 5 threads per scan with the calling thread |

## Large tree: 102k files in 2,221 folders

| One thread, every folder in turn | Run 1 | Run 2 |
|---|---|---|
| Listing alone | 430 | 431 |
| + a `CFileSystemObject` per entry | 503 | 501 |
| + the largest files | 519 | 520 |

| `calculateStatsFor`, 500 largest files | Run 1 | Run 2 |
|---|---|---|
| Pool of 1 worker: 2 threads | 252 | 251 |
| Pool of 2 workers: 3 threads | 221 | 218 |
| Pool of 4 workers: 5 threads | 208 | 211 |
| Pool of 4 workers, no largest files, as a folder size asks for | 192 | 196 |

| Statistics pool | Run 1 | Run 2 |
|---|---|---|
| Own threads | 212 | 223 |
| Mirror of the current scan | 216 | 217 |
| P3: one lock per folder, wakeups only for queued folders | 213 | 220 |
| Object per entry | 255 | 251 |

## Narrow tree: a chain of 60 folders, 20 files each

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor`, pool of 1 worker | 12.0 | 12.2 |
| `calculateStatsFor`, pool of 4 workers | 14.1 | 13.8 |
| `calculateStatsFor`, pool of 4 workers, no largest files | 10.3 | 10.5 |
| Own threads | 14.7 | 14.2 |
| Mirror of the current scan | 14.3 | 13.5 |
| P3 | 12.0 | 12.2 |

## Every folder's size: 1,000 folders of 5 files, each a task on the statistics pool, no largest files

| | Run 1 | Run 2 |
|---|---|---|
| `calculateStatsFor` | 117 | 120 |
| Own threads | 555 | 559 |
| Mirror of the current scan | 117 | 119 |
| P3 | 117 | 117 |

## Findings

- Listing is 83% of a one-thread scan of the large tree; building a `CFileSystemObject` per entry 14%, and the largest
  files 3%.
- Building a `CFileSystemObject` only for a file that enters the largest files makes the large tree's scan 14-15% faster
  than one for every entry.
- Without the largest files, the large tree's scan is another 7-8% faster.
- Sizing many small folders on the statistics pool is 4.7x faster than with own threads, whose start costs more than
  scanning a small folder. The large and narrow trees take the same time either way.
- P3 gains nothing measurable on the large tree, and up to 2 ms on the narrow tree.
