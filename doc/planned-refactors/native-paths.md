# Native filesystem paths

## Status

`CEntryPath` currently stores a normalized `QString`. The operation engine rejects POSIX names that cannot survive
a Qt decode/encode round trip, preventing wrong-entry mutation but not supporting those names. A native-path model
remains planned.

## Problem

POSIX filenames are byte sequences; not every legal name is valid UTF-8. `thin_io::list_directory()` preserves the
native bytes, but converting an enumerated name through `QString` can replace invalid bytes. Re-encoding can then
address no entry or, worse, a different entry with the same displayed spelling.

Windows native paths are UTF-16 and fit Qt's model. macOS normalization requires separate on-platform validation.
Passing `std::filesystem::path` to Qt does not solve the issue because Qt's filesystem API still converts it through
`QString`.

## Target invariants

Operation paths should keep native addressing separate from presentation:

1. Native bytes or UTF-16 are the sole input to native and `thin_io` filesystem calls.
2. Names discovered by enumeration are appended without a Unicode round trip.
3. User-entered names are validated and encoded once at the UI-to-operation boundary.
4. Equality, hashing, parent/child operations, and automatically derived destinations use native representation.
5. Display text is derived independently and is never re-encoded to recover an enumerated path.
6. Generated private names such as staging siblings use known-safe native ASCII.

`thin_io::native_string` already provides the platform split (`std::string` on POSIX and `std::wstring` on
Windows). Where Qt streaming remains useful, native code can open the exact path and attach `QFile` to the resulting
descriptor.

Do not encode raw bytes as unpaired UTF-16 surrogates inside `QString`; that implicit convention would leak into
normalization, serialization, clipboard, and unrelated UI code.

## Delivery

1. **Containment (implemented):** manifest construction rejects names that cannot round-trip, with Darwin-specific
   normalization handling. Such a name fails the manifest explicitly and is never mistaken for a vanished child.
2. **Disappearance races (implemented):** a non-root entry that genuinely vanishes during scanning is omitted;
   cancellation and failure remain distinct outcomes.
3. **Operation engine:** introduce a dual native/display path and migrate manifests, mutators, staged copy,
   destination resolution, diagnostics, and executors.
4. **Panel and UI:** carry native identity through filesystem objects, hashing, selection, drag/drop, persistence,
   and the plugin API.

Invalid bytes need an unambiguous escaped display form such as `\xNN`. Rename editing, clipboard/export spelling,
persisted paths, and plugin compatibility must be decided as part of UI delivery; display collisions must never
become identity collisions.

Verification must create hostile names through native APIs and prove exact-name copy, move, and delete; harmless
handling of genuine disappearance; separation of display-colliding names; and unchanged Windows behavior. macOS
expectations depend on an on-platform native-name probe.

## References

- [Qt filesystem path behavior](https://doc.qt.io/qt-6/qfile.html#platform-specific-issues)
- [QString invalid UTF-8 behavior](https://doc.qt.io/qt-6/qstring.html#fromUtf8-1)
