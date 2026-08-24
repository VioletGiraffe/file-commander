#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFontMetrics>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>

#include <cstdint>
#include <vector>

class CLightningFastViewerWidget final : public QAbstractScrollArea
{
public:
	enum Mode { HEX, TEXT };

	explicit CLightningFastViewerWidget(QWidget* parent = nullptr);

	void setData(const QByteArray& bytes);
	void setText(const QString& text);
	void setWordWrap(bool enabled);
	void setTabWidth(int columns);

	bool find(const QString& exp, QTextDocument::FindFlags options = {});
	bool find(const QRegularExpression& exp, QTextDocument::FindFlags options = {});
	void moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode = QTextCursor::MoveAnchor);
	int selectionStart() const;

protected:
	void paintEvent(QPaintEvent*) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	bool event(QEvent* event) override;
	void updateCursorShape(const QPoint& pos);

private:
	enum Region { REGION_OFFSET, REGION_HEX, REGION_ASCII, REGION_NONE };

	struct Selection
	{
		qsizetype start = -1;
		qsizetype end = -1;
		Region region = REGION_NONE;

		[[nodiscard]] inline bool isValid() const { return start >= 0 && end >= 0; }
		[[nodiscard]] inline qsizetype selStart() const { return qMin(start, end); }
		[[nodiscard]] inline qsizetype selEnd() const { return qMax(start, end); }
	};

	// Common methods
	[[nodiscard]] qsizetype totalLines() const;
	void updateLayoutAndScrollBars();
	void ensureVisible(qsizetype offset);
	void copySelection();
	void selectAll();
	[[nodiscard]] bool isSelected(qsizetype offset) const;
	void updateFontMetrics();
	void contentChanged();
	[[nodiscard]] qsizetype searchStartOffset(bool backward, qsizetype haystackSize) const;

	// Hex mode methods
	void calculateHexLayout();

	struct LineLayout {
		int hexStart = 0;
		int hexWidth = 0;
		int asciiStart = 0;
		int asciiWidth = 0;
	};
	[[nodiscard]] LineLayout calculateHexLineLayout(int bytesPerLine, int nDigits) const;
	void drawHexLine(QPainter& painter, qsizetype offset, int y, const QFontMetrics& fm);
	[[nodiscard]] Region regionAtPos(const QPoint& pos) const;
	[[nodiscard]] qsizetype hexPosToOffset(const QPoint& pos) const;

	// Text mode methods
	void drawTextLine(QPainter& painter, qsizetype lineIndex, int y, const QFontMetrics& fm);
	[[nodiscard]] qsizetype textPosToOffset(const QPoint& pos) const;

	// Columns occupied by ch starting at the given column. 'next' is the following character, which resolves CR-LF and surrogate pairs.
	[[nodiscard]] int columnsForChar(QChar ch, QChar next, qsizetype column) const;
	[[nodiscard]] int columnsForNonAsciiChar(QChar ch) const;

	void rebuildLineIndexIfNeeded();
	[[nodiscard]] qsizetype findLineContainingOffset(qsizetype offset) const;

private:
	Mode _mode = HEX;

	// Hex mode data
	QByteArray _data;
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
	QString _paintScratch; // Reused by drawTextLine: QPainter::drawText has no QStringView overload
	QFontMetrics _fontMetrics;
	Selection _selection;
	bool _geometrySettled = false; // The line index needs the real viewport width, which only exists after the first resize

	// Hex layout positions (calculated in calculateHexLayout)
	int _hexStart = 0;
	int _asciiStart = 0;
	int _nDigits = 0; // Cached number of digits for offset display
};
