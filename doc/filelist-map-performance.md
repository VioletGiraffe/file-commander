# File list map performance

Measured cost of the candidate containers for `FileListHashMap`, from `filelist_hashmap_benchmark`; how to run it:
[testing.md](testing.md), "File list map benchmark". Figures are the total of one listing (build, destroy, copy) in
ns per entry, medians; in brackets, against `segmented_map` with the same `reserve` setting. Re-measure after changing
`CFileSystemObject` or updating a container library.

## Setup

| | Windows PC | Raspberry Pi 4 |
|---|---|---|
| CPU | Core i5-12600K, pinned to one P-core | Cortex-A72, pinned to one core |
| OS | Windows 11 23H2 | Raspberry Pi OS (Debian 13.7) |
| Toolchain | MSVC 2022, Qt 6.11.2 | GCC 14, Qt 6.8.2 |
| Commit | dff64e89 | 046ca212 |
| Samples | 15 per cell | 15 per cell |

## Decision

`FileListHashMap` stays ankerl `segmented_map`:

- With `reserve`, ankerl `map` is within 3% of it on the Pi at every size, and within 6% on Windows from 10k up.
- Below 10k, ankerl `map` and Boost `unordered_flat_map` lead by up to 38% on Windows, where the map costs under 40 µs
  per listing. Boost `unordered_flat_map` trails by 13-80% from 10k up.
- Without `reserve`, as the flattened listing builds, it beats every candidate from 1k up on both machines.

## Windows

| With `reserve` | 100 | 1k | 10k | 100k | 1M |
|---|---|---|---|---|---|
| ankerl `segmented_map` | 55.0 | 37.3 | 41.8 | 66.5 | 81.0 |
| ankerl `map` | 37.7 (-32%) | 31.0 (-17%) | 43.2 (+3%) | 70.5 (+6%) | 85.1 (+5%) |
| Boost `unordered_flat_map` | 34.2 (-38%) | 33.5 (-10%) | 67.9 (+62%) | 84.2 (+27%) | 114.3 (+41%) |
| Boost `unordered_node_map` | 74.9 (+36%) | 73.1 (+96%) | 106.1 (+154%) | 184.4 (+177%) | 224.2 (+177%) |
| `std::unordered_map` | 89.1 (+62%) | 83.4 (+123%) | 111.5 (+167%) | 184.6 (+178%) | 263.4 (+225%) |
| `QHash` | 54.8 (-1%) | 58.3 (+56%) | 73.8 (+76%) | 123.8 (+86%) | 196.2 (+142%) |

| Without `reserve` | 100 | 1k | 10k | 100k | 1M |
|---|---|---|---|---|---|
| ankerl `segmented_map` | 75.3 | 52.4 | 61.6 | 82.0 | 107.3 |
| ankerl `map` | 80.3 (+7%) | 61.2 (+17%) | 86.1 (+40%) | 138.6 (+69%) | 153.6 (+43%) |
| Boost `unordered_flat_map` | 46.5 (-38%) | 62.2 (+19%) | 70.6 (+15%) | 101.1 (+23%) | 149.6 (+39%) |
| Boost `unordered_node_map` | 79.5 (+6%) | 82.3 (+57%) | 114.2 (+85%) | 189.1 (+131%) | 249.0 (+132%) |
| `std::unordered_map` | 102.9 (+37%) | 90.2 (+72%) | 143.8 (+133%) | 229.1 (+179%) | 350.7 (+227%) |
| `QHash` | 66.8 (-11%) | 75.7 (+44%) | 119.5 (+94%) | 163.4 (+99%) | 229.0 (+113%) |

## Raspberry Pi 4

| With `reserve` | 100 | 1k | 10k | 100k | 1M |
|---|---|---|---|---|---|
| ankerl `segmented_map` | 452.9 | 470.6 | 550.2 | 669.4 | 661.6 |
| ankerl `map` | 453.4 (+0%) | 460.3 (-2%) | 542.4 (-1%) | 651.1 (-3%) | 661.1 (-0%) |
| Boost `unordered_flat_map` | 431.8 (-5%) | 461.4 (-2%) | 621.4 (+13%) | 816.4 (+22%) | 1188.2 (+80%) |
| Boost `unordered_node_map` | 544.4 (+20%) | 577.8 (+23%) | 868.6 (+58%) | 1324.4 (+98%) | 1759.7 (+166%) |
| `std::unordered_map` | 538.1 (+19%) | 593.1 (+26%) | 841.3 (+53%) | 1278.7 (+91%) | 1428.2 (+116%) |
| `QHash` | 542.6 (+20%) | 600.0 (+28%) | 763.0 (+39%) | 1058.1 (+58%) | 1313.4 (+99%) |

| Without `reserve` | 100 | 1k | 10k | 100k | 1M |
|---|---|---|---|---|---|
| ankerl `segmented_map` | 454.2 | 494.7 | 585.0 | 711.4 | 832.6 |
| ankerl `map` | 487.2 (+7%) | 507.7 (+3%) | 822.9 (+41%) | 906.2 (+27%) | 1007.3 (+21%) |
| Boost `unordered_flat_map` | 466.4 (+3%) | 537.8 (+9%) | 835.5 (+43%) | 964.1 (+36%) | 1468.2 (+76%) |
| Boost `unordered_node_map` | 559.0 (+23%) | 612.9 (+24%) | 908.5 (+55%) | 1397.4 (+96%) | 1899.7 (+128%) |
| `std::unordered_map` | 583.8 (+29%) | 620.1 (+25%) | 924.3 (+58%) | 1787.1 (+151%) | 2096.6 (+152%) |
| `QHash` | 542.7 (+19%) | 647.1 (+31%) | 969.1 (+66%) | 1380.4 (+94%) | 1569.1 (+88%) |

## Observations

- ankerl `map`'s lead on small Windows maps follows the allocation count: at 100 entries `segmented_map` makes 9
  allocations to its 3, and costs 17 ns per entry more to build and destroy. On the Pi, with glibc's allocator, the
  difference is 3 ns.
- On the Pi from 100k up, Boost `unordered_flat_map` trails both ankerl maps in every phase, and looks up 2.4-2.6
  times slower than ankerl `map`. On Windows it looks up fastest of the three.
- Copying the values costs 214-243 ns per entry on the Pi with either ankerl map, about half the total, against 9-28
  ns on Windows. Most of it does not depend on the container, so the differences between containers are smaller on
  the Pi.
- Lookups are left out of the total. From 10k up, `segmented_map` looks up 15-47% slower than ankerl `map` on both
  machines.
- Without `reserve`, `segmented_map` peaks at the memory it ends with. ankerl `map` and Boost `unordered_flat_map` peak
  at 1.3-1.6 times it while rehashing.

## Noise

- Windows: IQR up to 80% at 100 entries and 94% at 1k, 2-10% from 10k up. Three reruns at 100 and 1k moved the ankerl
  medians by up to 6%, Boost `unordered_flat_map`'s by up to 21%.
- Pi: IQR under 1% in most cells, up to 7% at 100k.

## Open questions

- What makes copying a `CFileSystemObject` about 20 times slower on the Pi than on Windows up to 10k entries, against
  5-9 times for building and destroying.
