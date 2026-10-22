#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <memory>

class CFileViewer;

// The window around a plugin's viewer: title, File menu, Esc, geometry persistence. Deletes itself on close.
// Parentless: a window with a parent stays above it.
class CViewerWindow final : public QMainWindow
{
public:
	// viewerCategory keys the persisted geometry
	CViewerWindow(std::unique_ptr<CFileViewer> viewer, const QString& viewerCategory);

	// Sets the title from the path; the viewer shows a failure in place
	void loadFile(const QString& path);
	[[nodiscard]] const QString& filePath() const noexcept { return _filePath; }
	[[nodiscard]] CFileViewer& viewer() const noexcept { return *_viewer; }

private:
	CFileViewer* const _viewer;
	QString _filePath;
};
