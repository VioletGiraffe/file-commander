# Listing performance

Measured cost of listing one folder, from `listing_benchmark`; how to run it: [testing.md](testing.md), "Listing
benchmark". Figures are µs per entry, medians, on the benchmark's flat folders of 1k, 10k and 100k entries. Each table
names the commit it was measured at: re-measure after changing the listing code.

## Setup

| | Windows PC | Raspberry Pi 4 |
|---|---|---|
| CPU | Core i5-12600K, pinned to one P-core | Cortex-A72, pinned to one core |
| OS | Windows 11 23H2 | Raspberry Pi OS (Debian 13.7), kernel 6.18 |
| Toolchain | MSVC 2022, Qt 6.11.2 | GCC 14, Qt 6.8.2 |
| Volume | 512 MB fixed VHDX, NTFS, indexing off: one on an SSD, one on an HDD | 512 MB ext4 image on the SD card, loop device with direct I/O |
| Cold | 5 samples per cell | 5 samples per cell |
| Warm | 15 timed runs after 2 untimed | 15 timed runs after 2 untimed |

Variants:

- `qt`: `QDir::entryInfoList` with the panel's filters, unsorted.
- `qt` sorted: the same with `QDir`'s default name sort. Older tables only.
- `thinio`: `thin_io::list_directory`.
- `panel`: `listDirectoryForPanel`, the panel's own listing.

## Windows

- Commit 93a59545: thin_io uses `FIND_FIRST_EX_LARGE_FETCH`; `listDirectoryForPanel` sorts, and `CFileSystemObject`
  queries every entry again.
- Commit 17f7be81, thin_io f573124: `listDirectoryForPanel` lists unsorted and builds objects from the enumeration's
  data. A thin_io entry also carries times, permissions and a link target.
- Commit 505d2ea5, thin_io c0762bf: a smaller thin_io entry, with the link target in a `heap_optional` and times as
  plain timestamps, zero for unset. Only the `thinio` row: nothing else changed since 17f7be81.

| Cold, SSD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted, 93a59545 | 2.14 | 1.81 | 2.11 |
| `qt`, 93a59545 | 1.98 | 1.36 | 1.66 |
| `qt`, 17f7be81 | 1.90 | 1.45 | 2.20 |
| `thinio`, 93a59545 | 1.32 | 0.90 | 1.22 |
| `thinio`, 17f7be81 | 1.56 | 1.15 | 1.87 |
| `panel` sorted, 93a59545 | 36.9 | 34.9 | 35.5 |
| `panel`, 17f7be81 | 3.00 | 2.64 | 3.25 |

| Cold, HDD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted, 93a59545 | 2.99 | 3.00 | 4.26 |
| `qt`, 93a59545 | 2.68 | 3.84 | 4.36 |
| `qt`, 17f7be81 | 2.58 | 4.41 | 3.90 |
| `thinio`, 93a59545 | 2.40 | 2.03 | 3.67 |
| `thinio`, 17f7be81 | 2.28 | 2.10 | 3.43 |
| `panel` sorted, 93a59545 | 125 | 97.4 | 112 |
| `panel`, 17f7be81 | 3.82 | 3.71 | 4.83 |

Warm on the HDD volume agrees with the SSD within 10%.

| Warm, SSD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted, 93a59545 | 0.92 | 1.01 | 1.16 |
| `qt`, 93a59545 | 0.66 | 0.67 | 0.72 |
| `qt`, 17f7be81 | 0.69 | 0.73 | 0.69 |
| `thinio`, 93a59545 | 0.26 | 0.25 | 0.25 |
| `thinio`, 17f7be81 | 0.42 | 0.44 | 0.39 |
| `thinio`, 505d2ea5 | 0.33 | 0.33 | 0.32 |
| `panel` sorted, 93a59545 | 4.81 | 4.92 | 10.6 |
| `panel`, 17f7be81 | 1.78 | 1.87 | 1.75 |

## Raspberry Pi 4

Commit 5e83b087: `listDirectoryForPanel` sorts. Commit e8535a60: it lists unsorted.

| Cold, SD card | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted, 5e83b087 | 16.7 | 11.6 | 11.8 |
| `qt`, 5e83b087 | 15.1 | 7.88 | 6.78 |
| `qt`, e8535a60 | 12.6 | 8.86 | 6.74 |
| `thinio`, 5e83b087 | 7.17 | 6.65 | 5.22 |
| `thinio`, e8535a60 | 6.74 | 6.30 | 5.12 |
| `panel` sorted, 5e83b087 | 48.7 | 45.5 | 48.3 |
| `panel`, e8535a60 | 47.9 | 42.0 | 42.0 |

| Warm | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted, 5e83b087 | 4.71 | 5.96 | 7.65 |
| `qt`, 5e83b087 | 2.37 | 2.54 | 2.53 |
| `thinio`, 5e83b087 | 0.99 | 1.03 | 1.01 |
| `panel` sorted, 5e83b087 | 14.4 | 16.8 | 18.6 |
| `panel`, e8535a60 | 12.5 | 13.1 | 13.1 |

## Where the panel's time goes

100k entries, cold. Pi rows combine both commits: `qt` and `thinio` are the same code in both.

| Part | Derived as | Windows SSD, 93a59545 | Windows SSD, 17f7be81 | Pi, e8535a60 |
|---|---|---|---|---|
| Enumeration, CPU | `thinio` warm | 0.25 | 0.39 | 1.01 |
| `QFileInfo` list, CPU | `qt` warm - `thinio` warm | 0.46 | 0.30 | 1.52 |
| Name sort, CPU | `qt` sorted warm - `qt` warm | 0.44 | none | none |
| Panel's own CPU | `panel` warm - `qt` sorted warm (unsorted: - `qt` warm) | 9.47 | 1.06 | 10.6 |
| Directory reads | `qt` cold - `qt` warm | 0.94 | 1.51 | 4.21 |
| Per-entry metadata reads | `panel` cold - `panel` warm - directory reads | 23.9 | none | 24.7 |
| Total | `panel` cold | 35.5 | 3.25 | 42.0 |

- Windows, 93a59545: the metadata reads come from `CFileSystemObject` querying each entry again, although the
  enumeration already returns size, times and attributes. At 17f7be81 it uses the enumeration's data.
- thin_io f573124 costs 0.14 more per entry warm than at 93a59545: its entries carry times, permissions and a link
  target. The `QFileInfo` row, derived from it, is understated by as much. At 505d2ea5 the difference is 0.07.
- Linux: `readdir` returns only name and type, so each file needs one `stat` for its size; a folder needs none.
- At 93a59545 the panel held about 2 KB per entry on Windows, and its warm cost doubled at 100k. At 17f7be81 it holds
  1.1 KB per entry, and the warm cost is flat, as on the Pi.

## Measured costs

`FIND_FIRST_EX_LARGE_FETCH`, `thinio` on Windows, commit 6ce0b67e without it and 93a59545 with it:

| | 1k | 10k | 100k |
|---|---|---|---|
| Cold SSD, without | 5.10 | 4.62 | 4.66 |
| Cold SSD, with | 1.32 | 0.90 | 1.22 |
| Cold HDD, without | 12.1 | 17.0 | 37.1 |
| Cold HDD, with | 2.40 | 2.03 | 3.67 |
| Warm SSD, without | 0.237 | 0.239 | 0.238 |
| Warm SSD, with | 0.260 | 0.248 | 0.253 |

`QDir`'s name sort, warm: 0.26 / 0.33 / 0.44 on Windows, 2.35 / 3.42 / 5.12 on the Pi, for 1k / 10k / 100k.

## Noise

- Windows SSD: repeated cold runs agree within 2-4%. A/B comparisons belong here, within one session: `qt` at 100k
  moved by +33% between the 93a59545 and 17f7be81 sessions with its code unchanged.
- Windows HDD: per-cell CV 5-54%, and cell medians moved by -27% to +91% between two runs. Only large effects show.
- Pi SD card: CV 1-10% at 10k and 100k; up to 50% at 1k, where fixed per-folder costs dominate.

## Open questions

- Whether issuing the `stat` calls in inode order shortens the cold metadata reads on ext4: `readdir` returns names
  in hash order.
