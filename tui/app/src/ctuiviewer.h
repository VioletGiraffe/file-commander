#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TGroup
#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QByteArray>
#include <QString>
RESTORE_COMPILER_WARNINGS

class CTuiViewerContent;
class CTuiViewerStatusLine;
class TScrollBar;
enum class Panel;
struct SearchSpec;

// The viewer's help context: the hint row and the key bar show the viewer's keys while it is open
inline constexpr ushort hcViewer = 1000;

// The viewer's commands, which its keys and the items of the hint row and the key bar send
inline constexpr ushort cmFindInViewer = 2400, cmFindNextInViewer = 2401, cmFindPreviousInViewer = 2402, cmToggleLineWrap = 2403, cmFirstViewAs = 2404;

// In the order of their keys, 1 to 6, as in the GUI's text viewer
enum class ViewAs : ushort { DetectedEncoding, Latin1, SystemLocale, Utf8, Utf16, Hex };
inline constexpr int viewAsCount = (int)ViewAs::Hex + 1;

// The command that shows the file as viewAs
[[nodiscard]] constexpr ushort cmViewAs(const ViewAs viewAs) noexcept
{
	return (ushort)(cmFirstViewAs + (ushort)viewAs);
}

// Shows the file over the panels and the menu bar until Esc closes it. A file that cannot be read gets a message instead.
void viewFile(const QString& filePath);

// A file's text or bytes above a status row: how the file is shown, the last command's outcome and the position
class CTuiFileView final : public TGroup
{
public:
	// The scroll bar is the owner's, on its frame
	CTuiFileView(const TRect& bounds, TScrollBar* verticalScrollBar);

	// In the encoding detected, from the start. note: for the status row, empty for none.
	void showFile(QByteArray data, QString note = {});
	// No content, and the message in the status row
	void showError(QString message);
	// Says so when the file is not text in that encoding
	void showAs(ViewAs viewAs);
	void toggleWrap();
	void find(const SearchSpec& search, bool backward);

private:
	void updateStatusLine();

private:
	CTuiViewerContent* _content; // Owned by this group
	CTuiViewerStatusLine* _statusLine; // Owned by this group
	QString _message; // The last command's outcome
	bool _unreadable = false; // showError() emptied the view: the status row shows only its message
};

// Quick view reads at most this much of a file
inline constexpr qint64 quickViewMaxBytes = 20 * 1024 * 1024;

// A file over one side's panel. It never takes the focus: the mouse wheel and its scroll bar scroll it.
class CTuiQuickView final : public TWindow
{
public:
	CTuiQuickView(TPoint desktopSize, Panel side);

	// Takes the panel's bounds
	void calcBounds(TRect& bounds, TPoint delta) override;
	void handleEvent(TEvent& event) override;
	TPalette& getPalette() const override;

	[[nodiscard]] Panel side() const noexcept { return _side; }
	// Reads at most quickViewMaxBytes. The file already shown is not read again.
	void showFile(const QString& filePath);

private:
	CTuiFileView* _fileView; // Owned by this window
	const Panel _side;
	QString _filePath;
};
