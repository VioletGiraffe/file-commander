#pragma once

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QFontMetrics>

#include <vector>

class CLightningFastViewerWidget final : public QAbstractScrollArea
{
public:
	enum Mode { HEX, TEXT };

	explicit CLightningFastViewerWidget(QWidget* parent = nullptr);

	void setData(const QByteArray& bytes);
	void setText(const QString& text);
	void setWordWrap(bool enabled);

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
	void updateScrollBarsAndHexLayout();
	void ensureVisible(qsizetype offset);
	void copySelection();
	void selectAll();
	[[nodiscard]] bool isSelected(qsizetype offset) const;
	void updateFontMetrics();

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

	void wrapTextIfNeeded();
	void clearWrappingData();
	[[nodiscard]] qsizetype findLineContainingOffset(qsizetype offset) const;

private:
	Mode _mode = HEX;

	// Hex mode data
	QByteArray _data;
	qsizetype _bytesPerLine = 16;

	// Text mode data
	QString _text;
	std::vector<qsizetype> _lineOffsets; // Starting character offset for each wrapped line
	std::vector<qsizetype> _lineLengths; // Length of each wrapped line
	size_t _wordWrapParamsHash = 0;
	bool _wordWrap = true;

	// Common display data
	int _lineHeight = 0;
	int _charWidth = 0;
	qsizetype _tabWidthInChars = 4; // Default tab width in character positions
	QFontMetrics _fontMetrics;
	Selection _selection;
	bool _initialized = false;

	// Hex layout positions (calculated in calculateHexLayout)
	int _hexStart = 0;
	int _asciiStart = 0;
	int _nDigits = 0; // Cached number of digits for offset display
};
