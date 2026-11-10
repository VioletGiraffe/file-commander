#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

// The text a VT terminal shows after receiving a program's output. Colours and modes are ignored.
// Covers the sequences Turbo Vision and ConPTY write: cursor movement, erasing, inserting and deleting, line feeds that scroll.
// Keeps the clipboard writes too.
class CTerminalScreen
{
public:
	CTerminalScreen(int rows, int columns);

	void feed(QStringView text);

	[[nodiscard]] int rows() const noexcept { return (int)_lines.size(); }
	[[nodiscard]] const QString& row(int index) const { return _lines[(size_t)index]; }
	// Every row, numbered, for a test failure's report
	[[nodiscard]] QString text() const;
	[[nodiscard]] bool contains(QStringView text) const;

	// Answers to the program's cursor position queries, to be written back to it
	[[nodiscard]] QByteArray takeReplies();
	// The text of the last clipboard write (OSC 52)
	[[nodiscard]] const QString& clipboardText() const noexcept { return _clipboardText; }

private:
	// The index after the sequence starting at `start`, or -1 when it is incomplete
	[[nodiscard]] qsizetype escape(QStringView text, qsizetype start);
	void csi(QStringView parameters, QChar final);
	void osc(QStringView body);
	void put(QChar c);
	void lineFeed();
	void reverseLineFeed();
	void clear(int row, int fromColumn, int toColumn);
	void moveCursorTo(int row, int column);

private:
	std::vector<QString> _lines;
	const int _columns;
	int _row = 0, _column = 0;
	int _savedRow = 0, _savedColumn = 0;
	// Set by a character in the last column: the next character wraps to the next row first
	bool _wrapPending = false;
	QString _incompleteSequence;
	QByteArray _replies;
	QString _clipboardText;
};
