# Search query language

The search dialog and `CFileSearchEngine` jointly implement this user-visible language.
`nameFilterToRegex()` is authoritative for name matching.

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

## Contents

With Regex enabled, the query is PCRE as typed, with Unicode properties on: `\w`, `\d` and `\b` are not limited to
ASCII. Otherwise it is literal text. Content search has no wildcard dialect. Case-sensitive and whole-word options
apply to both modes.

Content is currently decoded as UTF-8, so equivalent text in UTF-16 or legacy encodings is not found. A chunk
boundary reads as a word boundary for whole-word matching, and `^` and `$` in content regexes also match at
internal chunk boundaries, so file-wide anchoring can over-report; line anchoring is unavailable.

An invalid regex stops before traversal and is reported separately from cancellation. A content query returns files
only, never directories.
