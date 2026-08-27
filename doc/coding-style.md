# Coding style

Authoring preferences for new code in this repo. Process doc, not architecture — for what the codebase
*is*, including its naming and notification conventions, see [README.md](README.md).

Keep comments terse, and challenge whether the code can carry the meaning instead. Do this every single time when writing a comment.

## C++ / Qt

- **QVariant with a built-in type** (int, uint64_t, QString, ...): pass it directly, e.g.
  `tabBar->setTabData(index, id)`. `QVariant::fromValue` / `.value<T>()` is machinery for non-built-in and
  registered types. Reading back, prefer the direct named accessor (`.toULongLong()`) over the template form.

- There is `mv(x)` macro (`cpp-template-utils/lang/utils.hpp`) for spots dense with moves, e.g. a constructor call moving many arguments where `mv` keeps it on one line. But default to `std::move(x)` — the idiomatic shape any reader understands. Don't convert existing code either way in passing.

- **No signals in the class, no `Q_OBJECT` in it.** A receiver needs no meta-object: `connect` asserts the
  macro only on the class that owns the *signal*, and a PMF slot is called directly. `Q_OBJECT` is still
  required for `Q_PROPERTY` / `Q_ENUM`, `qobject_cast` and `findChild<T*>`, string-based `connect` and
  `QMetaObject::invokeMethod`, and QSS type selectors — without it `metaObject()->className()` reports the
  nearest base, so `MyWidget { ... }` in a stylesheet stops matching. Translation context falls back to that
  base class too; that alone is not a reason to add the macro.

- A QStringBuilder expression must not be a ternary operand unless both branches have the same shape:
  `cond ? a % b : a % c % d` fails on clang — two unrelated `QStringBuilder<...>` instantiations with no
  common type — even though MSVC accepts it. Build the string imperatively, or wrap each branch in
  `QString{...}`.

## Concurrency

Any change touching threading or concurrency carries a mandatory dedicated review pass, separate from
reading the diff. The rule, what the pass checks, and why it pays for itself: [threading.md](threading.md).

Use `CInterruptableThread` for owned cancellable threads. Do not use `std::jthread`; it is unavailable in the
supported macOS toolchain.

## Tests

Do not interpolate Catch2 test names into filesystem paths. Test titles may contain characters illegal in
filenames on some platforms, such as `:` on Windows. Use a fixed `QTemporaryDir` template, or sanitize the title
explicitly when retaining it provides real diagnostic value.
