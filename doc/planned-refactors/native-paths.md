# Native filesystem paths

## Status and scope

This document records a planned direction, not the current path contract. `CEntryPath` currently owns one
normalized `QString`, and the file-operation bridge converts between it and `thin_io`'s native path strings. The
safety-containment step described below is implemented; transparent operation on non-round-tripping names remains
planned.

The immediate problem is on Linux and other POSIX systems whose filesystem APIs treat names as arbitrary byte
sequences: every byte except NUL and `/` is legal, but not every such sequence is valid UTF-8. Windows native
paths are UTF-16 and therefore fit Qt's `QString` model much more naturally. macOS behavior needs explicit
verification before assigning it the same support requirements as Linux; Qt applies Unicode normalization there,
and the existing `thin_io` invalid-UTF-8 enumeration test is excluded on Apple platforms.

## Motivation

`thin_io::list_directory()` already preserves a POSIX entry's native name bytes, but `CEntryPath` cannot retain
that property. Before safety containment, source-tree construction performed this conversion unconditionally:

```text
native name bytes
    -> QFile::decodeName()
    -> QString child name / CEntryPath
    -> QFile::encodeName()
    -> native syscall path
```

Qt uses Unicode strings for public filesystem paths. Invalid UTF-8 is replaced while decoding, so the last byte
sequence need not equal the first. Without the containment check, that causes two distinct safety defects:

1. If the re-encoded name addresses nothing, a regular file's metadata lookup returns `NotFound`. The source-tree
   builder currently mistakes that deterministic conversion failure for a file that vanished concurrently and
   silently omits it. A copy can consequently report success without copying every source entry.
2. If the re-encoded name addresses a different, valid entry whose spelling contains the replacement character,
   two native directory entries collapse to one `CEntryPath`. Copy may read the wrong file twice; permanent delete
   may remove the valid entry while processing the invalid-byte entry.

This also obscures the correct policy for a genuine scan race. A round-tripping child that really disappears after
its parent was listed should be tolerable whether it is a file or directory. A name that never round-tripped was
not a race and must not be silently dropped. The representation has to preserve that distinction.

The problem is not fixed by passing `std::filesystem::path` to Qt. Although `std::filesystem::path` preserves its
native narrow string on POSIX, Qt's overloads convert it to `QString` through a UTF-8 view before performing the
operation. Nor would choosing a different text codec solve the general problem: no Unicode encoding is a bijection
over arbitrary POSIX filename bytes while also displaying valid Unicode names correctly.

## Recommended design

Keep native identity/addressing separate from presentation. Native bytes must remain authoritative from directory
enumeration through every filesystem call; a `QString` is a display value and must never be re-encoded to recover
an enumerated path.

A path value used by the operation engine should conceptually contain:

```cpp
class CNativeOperationPath
{
public:
	const thin_io::native_string& nativeValue() const noexcept;
	const QString& displayValue() const noexcept;

	CNativeOperationPath parent() const;
	CNativeOperationPath childFromNativeName(const thin_io::native_string& nativeName) const;
	CNativeOperationPath childFromUserName(const QString& name) const;
	QString displayName() const;
};
```

The exact name and API are deferred. The important constraints are:

1. `nativeValue()` is the sole input to native and `thin_io` filesystem calls.
2. `childFromNativeName()` appends the bytes returned by enumeration without a Unicode round trip. It derives a
   display label independently.
3. `childFromUserName()` validates the user-visible component and encodes it once for the target platform.
4. Parent/child splitting and equality operate on the native representation. Display spelling cannot determine
   identity because two native paths may have the same decoded label.
5. Automatically derived transfer destinations preserve the source's native basename. A destination produced by
   an explicit Rename decision instead uses the newly entered Unicode name.
6. Generated private names, such as staging siblings, are appended directly as known-safe native ASCII.
7. Operation requests entering from the UI may begin as `QString` because the user supplied those paths. Entries
   discovered below that root must never return through `QString` before being addressed.

`thin_io::native_string` is already the appropriate platform split: `std::string` on POSIX and `std::wstring` on
Windows. Its primitives should accept that representation consistently. Where Qt stream facilities remain useful,
native code can open the exact path first and attach `QFile` to the resulting file descriptor; attaching a file
descriptor does not require Qt to interpret the filename.

Do not encode raw bytes as unpaired UTF-16 surrogates inside `QString`. Such a surrogate-escape convention would
be fragile across Qt UTF-8 conversion, normalization, settings serialization, clipboard transfer, and unrelated
UI code. An explicit native field makes the invariant visible and enforceable.

## Display policy

Invalid byte sequences still need an understandable, unambiguous label. The initial policy should decode valid
UTF-8 normally and escape only invalid bytes as `\xNN`. Display collisions must remain harmless because selection,
operation requests, and path hashes use the native identity rather than the displayed text.

Editing requires special care. Escaped display text must not silently become a literal backslash spelling when a
rename field is accepted. Viable choices are:

1. Treat the field as a proposed new Unicode name and make it visually explicit that accepting it renames the
   entry to that literal spelling.
2. Give invalid-byte names a read-only initial representation until the user begins an explicit rename.
3. Provide a dedicated escaped-name editor that round-trips `\xNN` sequences.

This decision matters only when direct panel interaction with such entries is implemented; recursive operations
can use the raw path while showing a non-editable escaped diagnostic.

## Incremental delivery

### 1. Safety containment (implemented)

The source-tree builder compares every enumerated POSIX name with
`encodeName(decodeName(nativeName))`. Darwin is the exception: Qt deliberately normalizes its path text, so the
builder validates that the native name is lossless UTF-8 before applying Qt's normalization instead of requiring
byte-for-byte equality. A failed check terminates the manifest build with an explicit unrepresentable-source-name
diagnostic. It is never classified as `NotFound`, skipped silently, or converted into a `CEntryPath`.

This prevents incomplete-success reports and wrong-entry mutations but deliberately does not operate on the
offending entry.

### 2. Genuine disappearance races (implemented)

Directory disappearance now matches file disappearance: a `list_directory()` `NotFound` result for a non-root
node means that child vanished and the parent scan continues. The selected root retains its existing
operation-level handling. The builder uses explicit `Node`, `ChildVanished`, and `BuildStopped` internal results;
cancellation, failure, and benign disappearance do not share an empty optional.

Each recursive frame restores `activeBranchTraversalIdentities` to its incoming depth on scope exit. These keys
combine the underlying entry with its mounted view so bind-mounted namespace views remain distinct during cycle
detection. Recovery from a vanished child therefore cannot leave stale cycle-detection state in the continuing
parent scan.

### 3. Native paths inside the operation engine

Introduce the dual-representation path type and migrate source-tree construction, snapshots, filesystem mutators,
staged copy, destination resolution, progress diagnostics, and copy/move/delete executors. Preserve native source
basenames when constructing automatic descendant destinations.

At this stage a recursively discovered invalid-byte entry can be copied, moved by copy-and-cleanup, or deleted
without being directly selectable in the panel.

### 4. Native identity through the panel and UI

The panel currently identifies entries through hashes derived from its string path model. Full user-facing support
requires native path identity to reach `CFileSystemObject`, selection state, drag/drop and operation-request
construction. Sorting, filtering, and painting may continue to use display strings; addressing and hashing may not.

This phase should also settle rename editing, clipboard/export spelling, persistence of paths containing invalid
bytes, and plugin-API compatibility. These concerns are why operation-engine containment should not wait for the
complete UI migration.

## Verification requirements

POSIX tests must create entries through native APIs rather than Qt so they genuinely contain hostile bytes.
Required cases include:

1. One regular file with an invalid UTF-8 basename.
2. An invalid-byte basename and a valid UTF-8 basename that decode to the same displayed `QString`.
3. Invalid-byte files, directories, file links, and directory links in both materializing-transfer and permanent-
   delete manifests.
4. Copy preserving the exact native basename and contents at the destination.
5. Delete and copy-based move acting on the intended native entry and no display-colliding sibling.
6. A round-tripping file disappearing after enumeration being omitted as a benign race.
7. A round-tripping subdirectory disappearing before its listing being omitted by the same policy.
8. A selected root disappearing during its listing retaining the existing root-specific outcome.
9. Display labels, sorting, selection, and hashes remaining distinct for native names with colliding Unicode
   renderings once UI support is added.

Windows tests should prove that the shared abstraction preserves existing UTF-16 behavior without introducing a
second encoding step. macOS support and test expectations must be decided from an on-platform native-name probe.

## References

- Qt documents that its I/O APIs represent paths with UTF-16 `QString` and encode Unix native paths as UTF-8:
  <https://doc.qt.io/qt-6/qfile.html#platform-specific-issues>.
- Qt documents that invalid UTF-8 input is replaced or suppressed when converted to `QString`:
  <https://doc.qt.io/qt-6/qstring.html#fromUtf8-1>.
- Qt's `std::filesystem::path` overloads convert a narrow native path through a UTF-8 string view:
  <https://github.com/qt/qtbase/blob/6.8/src/corelib/io/qfile.h#L58-L75>.
