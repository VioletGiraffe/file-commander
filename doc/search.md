# Search query language

The search dialog and `CFileSearchEngine` split one user-visible language between them, so neither states it
alone. This document is that language; `nameFilterToRegex()` is authoritative for the name half of it.

## Name filter

The field holds a `;`-separated list of filters, and a name matches if any one of them matches. A filter of
`*` alone matches every name, so it makes the rest of the list moot. An empty field matches any name, which is
how a contents-only query is expressed.

Four things are special in a filter:

| form | meaning |
|------|---------|
| `*` | any run of characters, including none |
| `?` | exactly one character |
| `^` | as the first character only: anchors to the start of the name |
| `$` | as the last character only: anchors to the end of the name |

Everything else is literal, including `.`, `+`, `(`, `[`, `{`, `|`, `\`, and a `^` or `$` anywhere but the
ends. There is no escape character and no regex mode, so nothing typed here can reach the regex engine as
syntax. An end left unanchored matches anywhere, which makes an unanchored filter a substring match.

## The "partial match" box

Unchecked, both ends of every filter are anchored, which also leaves a typed `^` or `$` as an ordinary
character:

| typed | checked (default) | unchecked |
|-------|-------------------|-----------|
| `abc` | contains | the whole name is `abc` |
| `^abc` | starts with | the whole name is `^abc` |
| `abc$` | ends with | the whole name is `abc$` |
| `^abc$` | the whole name is `abc` | the whole name is `^abc$` |
| `*.txt` | contains `.txt` | ends with `.txt` |
| `abc*` | starts with | starts with |

An already-anchored filter means the same thing in both modes, which is what makes it independent of the box.

## Contents

Two dialects:

| "Regex" | the query is |
|---------|--------------|
| checked | PCRE, exactly as typed |
| clear | literal text; no character has any special meaning |

Note the asymmetry with the name field: a contents query has no wildcards, in either dialect. "Case sensitive"
and "Whole words" apply to both. Whole-word matching is unreliable for a query whose first or last character is
punctuation, since a word boundary there demands a word character on the outside of it.

`^` and `$` in a contents regex do anchor to the start and end of the file, but they also match at regular
intervals inside it, so an anchored query over-reports: every file it should find, plus some it should not.
There is no way to anchor to a line.

Contents are matched as UTF-8, so only text stored as UTF-8 or ASCII can be found. A query will not match the
same text held in UTF-16 or in a legacy code page, and the search reports completion either way - there is no
indication that a file's encoding put its contents out of reach. This is a known and accepted limitation.

An invalid regex ends the search before any traversal and is reported as such, distinctly from a cancellation.
A contents query also restricts results to files: directories are never reported for one.
