#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>

// Absolute normalized operation path: '/' as the one internal separator, no trailing separator except where a
// filesystem root requires it ("/", "C:/"; a UNC share root is "//server/share"). Makes no claim about existence,
// type, writability, or identity. Constructed only by parseOperationPath() and by operations on already-valid
// paths, so the invariants hold by construction.
class CEntryPath
{
public:
	[[nodiscard]] const QString& value() const noexcept;
	[[nodiscard]] CEntryPath parent() const; // Must not be called on a root
	[[nodiscard]] CEntryPath child(const QString& name) const; // name: non-empty, no separators, not "." or ".."
	[[nodiscard]] QString name() const; // Last component; for a root, its full spelling
	[[nodiscard]] bool isRoot() const;

	// Spelling comparison under the platform case policy; filesystem identity comparison is a separate operation.
	[[nodiscard]] bool operator==(const CEntryPath& other) const noexcept;

private:
	explicit CEntryPath(QString normalizedPath) noexcept;
	friend std::optional<CEntryPath> parseOperationPath(QString path);

	QString _path;
};

// The only entry point for untrusted path text (confirmation-field edits, external drag-and-drop).
// Accepts absolute paths only: rooted paths on POSIX; drive-absolute ("C:\...") and UNC ("\\server\share\...")
// forms on Windows, either separator. Collapses duplicate separators and "."/".." components (".." clamps at the
// root) and trims surrounding whitespace as a typed-text artifact. All invalid inputs share one response: nullopt.
[[nodiscard]] std::optional<CEntryPath> parseOperationPath(QString path);

// Whether text is usable as one new entry name (a Rename decision, the child() precondition):
// non-empty, no separators, not "." or "..".
[[nodiscard]] bool isValidEntryName(const QString& name);
