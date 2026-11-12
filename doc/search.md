# Search query language

A search dialog's fields (`filesearchengine/filesearchquery.h`) and `CFileSearchEngine` jointly implement this
user-visible language. `nameFilterToRegex()` is authoritative for name matching.

## Name filter

The field is a `;`-separated OR-list. An empty field or `*` matches every name. Each filter supports:

| Form | Meaning |
|------|---------|
| `*` | Any run of characters, including none |
| `?` | Exactly one character |
| leading `^` | Anchor to the start of the name |
| trailing `$` | Anchor to the end of the name |

Everything else is literal. There is no escape syntax or regex mode. With partial matching enabled, unanchored ends
match anywhere; with it disabled, both ends are anchored and typed `^` or `$` are treated literally. Thus `abc`
means "contains" in partial mode and exact equality otherwise, while `^abc` and `abc$` explicitly select prefix and
suffix matching.

The panel's filter box uses the same language, ignoring case, always in partial mode (`FileNameFilter`,
`file-commander-core/src/filelist/`).

## Excluded folders

The same language, each filter matching the whole name, always ignoring case. A folder below the search roots that
matches is skipped with its contents: it is neither reported nor scanned. A root is searched even when its name matches.

## Contents

With Regex enabled, the query is PCRE as typed, with Unicode properties on: `\w`, `\d` and `\b` are not limited to
ASCII. Otherwise it is literal text. Content search has no wildcard dialect. Case-sensitive and whole-word options
apply to both modes.

Content is currently decoded as UTF-8, so equivalent text in UTF-16 or legacy encodings is not found. `^` and `$` in
content regexes anchor to the file's start and end; line anchoring is unavailable. The regex engine reads a file in
1 MiB blocks, each with 4 KiB of the text around it: a match longer than 4 KiB that crosses a block's edge is not found.

An invalid regex stops before traversal and is reported separately from cancellation. A file that cannot be read, or
whose contents exhaust the regex engine's backtracking budget, is counted as inconclusive rather than reported as a
non-match. A content query returns files only, never directories.
