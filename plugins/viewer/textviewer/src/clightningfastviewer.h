#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QTextDocument>

#include <array>
#include <cstdint>
#include <vector>

class CLightningFastViewerWidget final : public QAbstractScrollArea
{
public:
	explicit CLightningFastViewerWidget(QWidget* parent = nullptr);

	// The family this widget sets on itself, exposed so sibling views can match it. A font set by the caller afterwards still wins.
	[[nodiscard]] static QFont preferredFixedFont();

	void setData(const QByteArray& bytes);
	void setText(const QString& text);
	void setWordWrap(bool enabled);
	void setTabWidth(int columns);

	bool find(const QString& exp, QTextDocument::FindFlags options = {});
	bool find(const QRegularExpression& exp, QTextDocument::FindFlags options = {});
	void moveToStart();
	void moveToEnd();
	// Start of the selection, or -1 when nothing is selected
	[[nodiscard]] qsizetype selectionStart() const;

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void timerEvent(QTimerEvent* event) override;
	bool event(QEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;

private:
	enum class Mode { Hex, Text };
	enum class Region { Offset, Hex, Ascii, None };

	// Both ends are character indices in text mode and byte indices in hex mode. Without an anchor there is no selection, only a cursor.
	struct Selection
	{
		qsizetype cursor = -1; // Moving end, and the cell the cursor block paints on; -1 until placed
		qsizetype anchor = -1; // Fixed end; -1 when nothing is selected
		qsizetype desiredColumn = -1; // Display column vertical movement returns to; every other cursor change drops it
		Region region = Region::None;

		[[nodiscard]] bool hasCursor() const { return cursor >= 0; }
		[[nodiscard]] bool hasSelection() const { return anchor >= 0; }

		// The three below are only meaningful while hasSelection()
		[[nodiscard]] qsizetype first() const { return qMin(anchor, cursor); }
		[[nodiscard]] qsizetype last() const { return qMax(anchor, cursor); } // Inclusive
		[[nodiscard]] qsizetype count() const { return last() - first() + 1; }

		// keepColumn defaults to dropping the sticky column: only a caller that moved the cursor vertically passes one on
		void placeCursor(qsizetype offset, qsizetype keepColumn = -1) { cursor = offset; anchor = -1; desiredColumn = keepColumn; }

		void extendTo(qsizetype offset, qsizetype keepColumn = -1)
		{
			if (!hasSelection())
				anchor = cursor >= 0 ? cursor : 0; // Extending from an unplaced cursor starts at the beginning
			cursor = offset;
			desiredColumn = keepColumn;
		}

		// A zero length places the cursor without selecting
		void selectRange(qsizetype from, qsizetype length, Region inRegion)
		{
			region = inRegion;
			anchor = length > 0 ? from : -1;
			cursor = length > 0 ? from + length - 1 : from;
			desiredColumn = -1;
		}
	};

	// Common methods
	[[nodiscard]] qsizetype totalLines() const;
	[[nodiscard]] int visibleLines() const;
	void updateLayoutAndScrollBars();
	void ensureVisible(qsizetype offset);
	// Renders the selected bytes as the given column shows them. Text mode has only its own text, so it ignores the format.
	void copySelection(Region format);
	void selectAll();
	[[nodiscard]] bool isSelected(qsizetype offset) const;
	void extendSelectionToDragPos();
	void autoScroll();
	void stopAutoScroll();
	void endDrag();
	void updateCursorShape(const QPoint& pos);
	void moveCursorTo(qsizetype offset);
	void updateFontMetrics();
	void contentChanged();
	[[nodiscard]] qsizetype searchStartOffset(bool backward, qsizetype haystackSize) const;
	// The text a regex search runs over. In hex mode the bytes are converted once and kept until the content changes.
	[[nodiscard]] const QString& regexHaystack();
	// _data with every byte case-folded, converted once and kept until the content changes
	[[nodiscard]] const QByteArray& foldedData();

	// Hex mode methods
	void calculateHexLayout();

	struct LineLayout {
		int hexStart = 0;
		int asciiStart = 0;
		int totalWidth = 0;
	};
	[[nodiscard]] LineLayout calculateHexLineLayout(int bytesPerLine) const;
	void drawHexLine(QPainter& painter, qsizetype offset, int y);
	[[nodiscard]] Region regionAtPos(const QPoint& pos) const;
	// Byte at pos read within the given column, so a drag stays in the column it started in. An x outside that column clamps to the line's first or last byte.
	[[nodiscard]] qsizetype hexPosToOffset(const QPoint& pos, Region region) const;

	// Text mode methods
	void drawTextLine(QPainter& painter, qsizetype lineIndex, int y);
	[[nodiscard]] qsizetype textPosToOffset(const QPoint& pos) const;

	// Offset in the line whose columns cover targetColumn, or the line's last offset when the column is past its end
	[[nodiscard]] qsizetype offsetAtColumn(qsizetype line, qsizetype targetColumn) const;
	// Display column at which 'offset' starts. 'line' must be the one containing it.
	[[nodiscard]] qsizetype columnOfOffset(qsizetype line, qsizetype offset) const;
	// Offset lineDelta lines away from the one holding fromOffset, at 'column'. Derives 'column' from fromOffset when it is negative on entry.
	[[nodiscard]] qsizetype offsetLinesAway(qsizetype fromOffset, qsizetype lineDelta, qsizetype& column) const;

	// Columns occupied by ch starting at the given column. 'next' is the following character, which resolves CR-LF and surrogate pairs.
	[[nodiscard]] int columnsForChar(QChar ch, QChar next, qsizetype column) const;
	[[nodiscard]] int columnsForNonAsciiChar(QChar ch) const;
	[[nodiscard]] QChar nonPrintableGlyph(QChar ch) const;
	void buildGlyphTables();

	void rebuildLineIndexIfNeeded();
	[[nodiscard]] qsizetype findLineContainingOffset(qsizetype offset) const;

private:
	Mode _mode = Mode::Hex;

	// Hex mode data
	QByteArray _data;
	QString _hexSearchText;    // Latin-1 view of _data, built on demand by regexHaystack()
	QByteArray _foldedData;    // Case-folded _data, built on demand by foldedData()
	qsizetype _bytesPerLine = 16;

	// Text mode data
	QString _text;
	// Visual line starts, with a trailing sentinel equal to _text.size(): line i spans [_lineOffsets[i], _lineOffsets[i + 1]).
	std::vector<qsizetype> _lineOffsets;
	qsizetype _maxLineColumns = 0;
	qsizetype _wrappedForMaxColumns = -1; // Line width the index was built for; -1 while the index is stale
	bool _wordWrap = true;

	// Common display data
	int _lineHeight = 0;
	int _charWidth = 0;
	int _tabWidth = 4;
	// Columns per BMP code point, memoized. Empty until the first non-ASCII character, cleared on font change.
	mutable std::vector<uint8_t> _charColumns;
	QString _paintScratch; // Reused by both painters: QPainter::drawText has no QStringView overload. Emptied with resize(0), which keeps the buffer.

	// Stand-in glyphs, rebuilt per font: every entry is known to exist in it and to measure exactly one column.
	std::array<QChar, 256> _nonPrintableGlyphs; // Text mode, indexed by code point
	std::array<QChar, 256> _hexGlyphs;          // Hex mode ASCII column, indexed by byte
	QChar _invisibleMarker;                     // Text mode, for the non-printables above U+00FF
	QFontMetrics _fontMetrics;
	Selection _selection;
	QPoint _dragPos;          // Last position of a drag in progress, in viewport coordinates
	int _autoScrollTimer = 0; // startTimer id while a drag is past a viewport edge; 0 otherwise
	bool _dragging = false;   // Set only by a press that landed on content, so a drag cannot resume an older selection
	bool _geometrySettled = false; // The line index needs the real viewport width, which only exists after the first resize

	// Hex layout positions
	int _hexStart = 0;
	int _asciiStart = 0;
	int _nDigits = 0; // Cached number of digits for offset display
};
