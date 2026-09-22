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
- `qt` sorted: the same with `QDir`'s default name sort.
- `thinio`: `thin_io::list_directory`.
- `panel`: `listDirectoryForPanel`, the panel's own listing.

## Windows

Commit 93a59545: thin_io uses `FIND_FIRST_EX_LARGE_FETCH`; `listDirectoryForPanel` still sorts.

| Cold, SSD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted | 2.14 | 1.81 | 2.11 |
| `qt` | 1.98 | 1.36 | 1.66 |
| `thinio` | 1.32 | 0.90 | 1.22 |
| `panel` sorted | 36.9 | 34.9 | 35.5 |

| Cold, HDD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted | 2.99 | 3.00 | 4.26 |
| `qt` | 2.68 | 3.84 | 4.36 |
| `thinio` | 2.40 | 2.03 | 3.67 |
| `panel` sorted | 125 | 97.4 | 112 |

| Warm, SSD | 1k | 10k | 100k |
|---|---|---|---|
| `qt` sorted | 0.92 | 1.01 | 1.16 |
| `qt` | 0.66 | 0.67 | 0.72 |
| `thinio` | 0.26 | 0.25 | 0.25 |
| `panel` sorted | 4.81 | 4.92 | 10.6 |

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

| Part | Derived as | Windows SSD, 93a59545 | Pi, e8535a60 |
|---|---|---|---|
| Enumeration, CPU | `thinio` warm | 0.25 | 1.01 |
| `QFileInfo` list, CPU | `qt` warm - `thinio` warm | 0.46 | 1.52 |
| Name sort, CPU | `qt` sorted warm - `qt` warm | 0.44 | none |
| Panel's own CPU | `panel` warm - `qt` sorted warm (Pi: - `qt` warm) | 9.47 | 10.6 |
| Directory reads | `qt` cold - `qt` warm | 0.94 | 4.21 |
| Per-entry metadata reads | `panel` cold - `panel` warm - directory reads | 23.9 | 24.7 |
| Total | `panel` cold | 35.5 | 42.0 |

- Windows, up to e8535a60: the metadata reads come from `CFileSystemObject` querying each entry again, although the
  enumeration already returns size, times and attributes. Later commits use the enumeration's data.
- Linux: `readdir` returns only name and type, so each file needs one `stat` for its size; a folder needs none.
- The panel holds about 2 KB per entry on Windows. Its warm cost doubles at 100k there, and stays flat on the Pi.

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

- Windows SSD: repeated cold runs agree within 2-4%. A/B comparisons belong here.
- Windows HDD: per-cell CV 5-54%, and cell medians moved by -27% to +91% between two runs. Only large effects show.
- Pi SD card: CV 1-10% at 10k and 100k; up to 50% at 1k, where fixed per-folder costs dominate.

## Open questions

- Whether issuing the `stat` calls in inode order shortens the cold metadata reads on ext4: `readdir` returns names
  in hash order.
