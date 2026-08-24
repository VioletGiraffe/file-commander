#include "clightningfastviewer.h"
#include "assert/advanced_assert.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QtMath>

#include <algorithm>
#include <limits>

static constexpr char hexChars[] = "0123456789ABCDEF";

namespace Layout {
	// Layout constants
	static constexpr int MIN_OFFSET_DIGITS = 4;
	static constexpr int OFFSET_SUFFIX_CHARS = 2; // ": "
	static constexpr int HEX_CHARS_PER_BYTE = 3; // "XX "
	static constexpr int HEX_MIDDLE_EXTRA_SPACE = 1; // Extra space after each 8 bytes
	static constexpr int HEX_ASCII_SEPARATOR_CHARS = 2; // The separator is technically " | ",
	// but we get the space on the left by default due to it being included with every byte via HEX_CHARS_PER_BYTE
	static constexpr int LEFT_MARGIN_PIXELS = 2;
	static constexpr int TEXT_HORIZONTAL_MARGIN_CHARS = 2;

	// Line width used when word wrap is off: never reached, and constant, so resizing cannot invalidate the index.
	static constexpr qsizetype NO_WRAP_COLUMNS = std::numeric_limits<qsizetype>::max();
}

// Stand-in for a character that has no printable glyph of its own.
static QChar displayCharForNonPrintable([[maybe_unused]] QChar ch)
{
	return QChar('.');
}

// True for characters that must be painted as a stand-in: no glyph, or a glyph that shows nothing.
static bool isSubstituted(QChar ch)
{
	const char16_t code = ch.unicode();
	if (code < 0x20 || code == 0x7F || (code >= 0x80 && code <= 0x9F))
		return true;
	if (code == 0xA0 || code == 0xAD) // No-break space and soft hyphen: nominally printable, invisible in practice
		return true;

	const auto category = ch.category();
	return category == QChar::Other_Format || category == QChar::Separator_Line || category == QChar::Separator_Paragraph;
}

CLightningFastViewerWidget::CLightningFastViewerWidget(QWidget* parent)
	: QAbstractScrollArea(parent), _fontMetrics(font())
{
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	viewport()->setMouseTracking(true);
	updateFontMetrics();
}

void CLightningFastViewerWidget::setData(const QByteArray& bytes)
{
	_mode = HEX;
	_data = bytes;
	_text.clear();

	contentChanged();
}

void CLightningFastViewerWidget::setText(const QString& text)
{
	_mode = TEXT;
	_text = text;
	_data.clear();

	contentChanged();
}

void CLightningFastViewerWidget::setWordWrap(bool enabled)
{
	if (_wordWrap == enabled)
		return;

	_wordWrap = enabled;
	rebuildLineIndexIfNeeded();
	updateLayoutAndScrollBars();
	viewport()->update();
}

void CLightningFastViewerWidget::setTabWidth(int columns)
{
	const int tabWidth = qMax(1, columns);
	if (_tabWidth == tabWidth)
		return;

	_tabWidth = tabWidth;
	_wrappedForMaxColumns = -1;
	rebuildLineIndexIfNeeded();
	updateLayoutAndScrollBars();
	viewport()->update();
}

void CLightningFastViewerWidget::contentChanged()
{
	_selection = Selection();
	_lineOffsets.clear();
	_maxLineColumns = 0;
	_wrappedForMaxColumns = -1;

	verticalScrollBar()->setValue(0);
	horizontalScrollBar()->setValue(0);

	rebuildLineIndexIfNeeded();
	updateLayoutAndScrollBars();
	viewport()->update();
}

void CLightningFastViewerWidget::paintEvent(QPaintEvent*)
{
	QPainter painter(viewport());
	painter.setFont(font());

	// Calculate which lines are visible
	const qsizetype firstLine = verticalScrollBar()->value();
	const qsizetype visibleLines = viewport()->height() / _lineHeight + 2;
	const qsizetype lastLine = qMin(firstLine + visibleLines, totalLines());

	// Draw lines based on mode
	int y = 0;
	for (qsizetype line = firstLine; line < lastLine; ++line)
	{
		if (_mode == HEX)
		{
			const qsizetype offset = line * _bytesPerLine;
			drawHexLine(painter, offset, y, _fontMetrics);
		}
		else
		{
			drawTextLine(painter, line, y, _fontMetrics);
		}
		y += _lineHeight;
	}
}

void CLightningFastViewerWidget::resizeEvent(QResizeEvent* event)
{
	QAbstractScrollArea::resizeEvent(event);
	_geometrySettled = true;
	rebuildLineIndexIfNeeded();
	updateLayoutAndScrollBars();
}

bool CLightningFastViewerWidget::event(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::FontChange:
	{
		updateFontMetrics();
		rebuildLineIndexIfNeeded();
		updateLayoutAndScrollBars();
		viewport()->update();
		break;
	}
	case QEvent::Show:
	{
		QFontInfo fi{ font() };
		assert_r(fi.fixedPitch());
		break;
	}
	default:
		break;
	}

	return QAbstractScrollArea::event(event);
}

void CLightningFastViewerWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		qsizetype offset = (_mode == HEX) ? hexPosToOffset(event->pos()) : textPosToOffset(event->pos());
		if (offset >= 0)
		{
			_selection.start = _selection.end = offset;
			_selection.region = (_mode == HEX) ? regionAtPos(event->pos()) : REGION_ASCII;
			viewport()->update();
		}
	}
}

void CLightningFastViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
	updateCursorShape(event->pos());

	if (event->buttons() & Qt::LeftButton && _selection.start >= 0)
	{
		qsizetype offset = (_mode == HEX) ? hexPosToOffset(event->pos()) : textPosToOffset(event->pos());
		if (offset >= 0 && offset != _selection.end)
		{
			_selection.end = offset;
			viewport()->update();

			// Auto-scroll when dragging near edges
			QRect viewRect = viewport()->rect();
			if (event->pos().y() < 0)
			{
				verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
			}
			else if (event->pos().y() > viewRect.height())
			{
				verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
			}
		}
	}
}

void CLightningFastViewerWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		if (_mode == HEX)
		{
			// Select the entire line on double-click
			qsizetype offset = hexPosToOffset(event->pos());
			if (offset >= 0 && offset < _data.size())
			{
				qsizetype lineStart = (offset / _bytesPerLine) * _bytesPerLine;
				qsizetype lineEnd = qMin((qsizetype)_data.size() - 1, lineStart + _bytesPerLine - 1);
				_selection.start = lineStart;
				_selection.end = lineEnd;
				_selection.region = regionAtPos(event->pos());
				viewport()->update();
			}
		}
		else
		{
			// Select word in text mode
			qsizetype offset = textPosToOffset(event->pos());
			if (offset >= 0 && offset < _text.size())
			{
				// Find word boundaries
				qsizetype start = offset;
				qsizetype end = offset;

				while (start > 0 && _text[start - 1].isLetterOrNumber())
					--start;
				while (end < _text.size() - 1 && _text[end + 1].isLetterOrNumber())
					++end;

				_selection.start = start;
				_selection.end = end;
				_selection.region = REGION_ASCII;
				viewport()->update();
			}
		}
	}
}

void CLightningFastViewerWidget::keyPressEvent(QKeyEvent* event)
{
	bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
	bool ctrlPressed = event->modifiers() & Qt::ControlModifier;

	if (ctrlPressed && event->key() == Qt::Key_C)
	{
		copySelection();
		return;
	}

	if (ctrlPressed && event->key() == Qt::Key_A)
	{
		selectAll();
		return;
	}

	// Navigation
	const qsizetype maxOffset = (_mode == HEX) ? _data.size() - 1 : _text.size() - 1;
	qsizetype cursorPos = _selection.end >= 0 ? _selection.end : 0;
	qsizetype newPos = cursorPos;

	switch (event->key())
	{
	case Qt::Key_Left:
		newPos = qMax(0LL, cursorPos - 1);
		break;
	case Qt::Key_Right:
		newPos = qMin(maxOffset, cursorPos + 1);
		break;
	case Qt::Key_Up:
		if (_mode == HEX)
		{
			newPos = qMax(0LL, cursorPos - _bytesPerLine);
		}
		else
		{
			// Find previous line
			const qsizetype currentLine = findLineContainingOffset(cursorPos);
			if (currentLine > 0)
			{
				// Try to maintain column position
				const qsizetype colInLine = cursorPos - _lineOffsets[currentLine];
				newPos = qMin(_lineOffsets[currentLine - 1] + colInLine, _lineOffsets[currentLine] - 1);
			}
		}
		break;
	case Qt::Key_Down:
		if (_mode == HEX)
		{
			newPos = qMin(maxOffset, cursorPos + _bytesPerLine);
		}
		else
		{
			// Find next line
			const qsizetype currentLine = findLineContainingOffset(cursorPos);
			if (currentLine >= 0 && currentLine + 1 < totalLines())
			{
				// Try to maintain column position
				const qsizetype colInLine = cursorPos - _lineOffsets[currentLine];
				newPos = qMin(_lineOffsets[currentLine + 1] + colInLine, _lineOffsets[currentLine + 2] - 1);
			}
		}
		break;
	case Qt::Key_PageUp:
		newPos = qMax(0LL, cursorPos - _bytesPerLine * (viewport()->height() / _lineHeight));
		break;
	case Qt::Key_PageDown:
		newPos = qMin(maxOffset, cursorPos + _bytesPerLine * (viewport()->height() / _lineHeight));
		break;
	case Qt::Key_Home:
		if (ctrlPressed)
		{
			newPos = 0;
		}
		else if (_mode == HEX)
		{
			newPos = (cursorPos / _bytesPerLine) * _bytesPerLine;
		}
		else
		{
			// Find start of current line
			const qsizetype currentLine = findLineContainingOffset(cursorPos);
			if (currentLine >= 0)
			{
				newPos = _lineOffsets[currentLine];
			}
		}
		break;
	case Qt::Key_End:
		if (ctrlPressed)
		{
			newPos = maxOffset;
		}
		else if (_mode == HEX)
		{
			qsizetype lineStart = (cursorPos / _bytesPerLine) * _bytesPerLine;
			newPos = qMin(maxOffset, lineStart + _bytesPerLine - 1);
		}
		else
		{
			// Find end of current line
			const qsizetype currentLine = findLineContainingOffset(cursorPos);
			if (currentLine >= 0)
			{
				newPos = qMin(maxOffset, _lineOffsets[currentLine + 1] - 1);
			}
		}
		break;
	default:
		QAbstractScrollArea::keyPressEvent(event);
		return;
	}

	if (newPos != cursorPos)
	{
		if (shiftPressed)
		{
			if (_selection.start < 0) _selection.start = cursorPos;
			_selection.end = newPos;
		}
		else
		{
			_selection.start = _selection.end = newPos;
		}
		ensureVisible(newPos);
		viewport()->update();
	}
}

qsizetype CLightningFastViewerWidget::totalLines() const
{
	if (_mode == HEX)
	{
		if (_data.isEmpty())
			return 0;
		return (_data.size() + _bytesPerLine - 1) / _bytesPerLine;
	}
	else
	{
		return _lineOffsets.empty() ? 0 : static_cast<qsizetype>(_lineOffsets.size()) - 1; // The last entry is the end sentinel
	}
}

void CLightningFastViewerWidget::calculateHexLayout()
{
	_nDigits = static_cast<int>(qCeil(::log10(static_cast<double>(_data.size() + 1))));
	_nDigits = qMax(Layout::MIN_OFFSET_DIGITS, _nDigits);

	const int viewportWidth = viewport()->width();
	int optimalBytesPerLine = 4; // Minimum
	LineLayout optimalLayout;

	// Try increasingly larger values (multiples of 4) until we exceed viewport width
	for (int candidate = 4; candidate <= 128; candidate += 4)
	{
		const auto layout = calculateHexLineLayout(candidate, _nDigits);
		const int lineWidth = layout.asciiStart + layout.asciiWidth;
		if (lineWidth >= viewportWidth)
			break;

		optimalBytesPerLine = candidate;
		optimalLayout = layout;
	}

	_bytesPerLine = optimalBytesPerLine;
	_hexStart = optimalLayout.hexStart;
	_asciiStart = optimalLayout.asciiStart;
}

CLightningFastViewerWidget::LineLayout CLightningFastViewerWidget::calculateHexLineLayout(int bytesPerLine, int nDigits) const
{
	LineLayout layout;

	layout.hexStart = Layout::LEFT_MARGIN_PIXELS + (nDigits + Layout::OFFSET_SUFFIX_CHARS) * _charWidth;
	layout.hexWidth = (bytesPerLine * Layout::HEX_CHARS_PER_BYTE + Layout::HEX_MIDDLE_EXTRA_SPACE * ((bytesPerLine - 1) / 8)) * _charWidth;
	layout.asciiStart = layout.hexStart + layout.hexWidth + Layout::HEX_ASCII_SEPARATOR_CHARS * _charWidth;
	layout.asciiWidth = bytesPerLine * _charWidth;

	return layout;
}

void CLightningFastViewerWidget::drawHexLine(QPainter& painter, qsizetype offset, int y, const QFontMetrics& fm)
{
	if (offset >= _data.size())
		return;

	// Account for horizontal scrolling
	const int hScroll = horizontalScrollBar()->value();

	// Draw offset
	const QString offsetStr = QStringLiteral("%1:").arg(offset, _nDigits, 10, QChar('0'));

	painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
	painter.drawText(Layout::LEFT_MARGIN_PIXELS - hScroll, y + fm.ascent(), offsetStr);

	painter.setPen(palette().color(QPalette::Text));

	const qsizetype lineBytes = qMin(static_cast<qsizetype>(_bytesPerLine), _data.size() - offset);

	QString hexByteString;
	hexByteString.resize(2);

	const QPen highlightPen(palette().highlightedText().color());
	const QPen normalPen(palette().color(QPalette::Text));

	int x = _hexStart;
	for (qsizetype i = 0; i < lineBytes; ++i)
	{
		const qsizetype byteOffset = offset + i;

		if (isSelected(byteOffset))
		{
			QRect selRect(x - hScroll, y, _charWidth * 2, _lineHeight);
			painter.fillRect(selRect, palette().highlight());
			painter.setPen(highlightPen);
		}
		else
		{
			painter.setPen(normalPen);
		}

		const uint8_t byte = static_cast<uint8_t>(_data[offset + i]);
		hexByteString[0] = hexChars[byte >> 4];
		hexByteString[1] = hexChars[byte & 0x0F];

		painter.drawText(x - hScroll, y + fm.ascent(), hexByteString);
		x += _charWidth * Layout::HEX_CHARS_PER_BYTE;

		if ((i % 8) == 7)
		{
			x += _charWidth * Layout::HEX_MIDDLE_EXTRA_SPACE;
		}
	}

	// Draw separator
	painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
	painter.drawText(_asciiStart - _charWidth * Layout::HEX_ASCII_SEPARATOR_CHARS - hScroll, y + fm.ascent(), QChar('|'));

	// Draw ASCII
	x = _asciiStart;
	for (qsizetype i = 0; i < lineBytes; ++i)
	{
		const unsigned char byte = static_cast<unsigned char>(_data[offset + i]);
		qsizetype byteOffset = offset + i;

		if (isSelected(byteOffset))
		{
			QRect selRect(x - hScroll, y, _charWidth, _lineHeight);
			painter.fillRect(selRect, palette().highlight());
			painter.setPen(highlightPen);
		}
		else
		{
			painter.setPen(normalPen);
		}

		const QChar ch = (byte >= 32 && byte <= 126) ? QChar(byte) : QChar('.');
		painter.drawText(x - hScroll, y + fm.ascent(), ch);
		x += _charWidth;
	}
}

void CLightningFastViewerWidget::drawTextLine(QPainter& painter, qsizetype lineIndex, int y, const QFontMetrics& fm)
{
	const qsizetype lineStart = _lineOffsets[lineIndex];
	const qsizetype lineEnd = _lineOffsets[lineIndex + 1];
	const qsizetype textLength = _text.size();

	const int baseline = y + fm.ascent();
	const int originX = Layout::LEFT_MARGIN_PIXELS - horizontalScrollBar()->value();

	const QColor normalColor = palette().color(QPalette::Text);
	const QColor selectedColor = palette().highlightedText().color();

	qsizetype column = 0;
	qsizetype runStart = -1; // Offset where the pending run of literal ASCII begins; -1 when there is no pending run
	qsizetype runColumn = 0;
	bool runSelected = false;

	const auto highlight = [&](qsizetype fromColumn, qsizetype toColumn) {
		painter.fillRect(originX + int(fromColumn) * _charWidth, y, int(toColumn - fromColumn) * _charWidth, _lineHeight, palette().highlight());
	};

	const auto drawChars = [&](const QChar* chars, qsizetype count, qsizetype atColumn, bool selected) {
		_paintScratch.resize(count);
		std::copy_n(chars, count, _paintScratch.data());
		painter.setPen(selected ? selectedColor : normalColor);
		painter.drawText(originX + int(atColumn) * _charWidth, baseline, _paintScratch);
	};

	const auto flushRun = [&] {
		if (runStart < 0)
			return;

		if (runSelected)
			highlight(runColumn, column);

		drawChars(_text.constData() + runStart, column - runColumn, runColumn, runSelected);
		runStart = -1;
	};

	const int viewportWidth = viewport()->width();

	for (qsizetype offset = lineStart; offset < lineEnd; ++offset)
	{
		if (originX + int(column) * _charWidth >= viewportWidth) // Columns only grow, so nothing past here is visible
			break;

		const QChar ch = _text[offset];
		const int columns = columnsForChar(ch, offset + 1 < textLength ? _text[offset + 1] : QChar(), column);
		if (columns == 0)
			continue;

		const bool selected = isSelected(offset);

		// Literal ASCII accumulates into a run: one drawText for the whole stretch instead of one per character
		if (const char16_t code = ch.unicode(); code >= 0x20 && code < 0x7F)
		{
			if (runStart >= 0 && selected != runSelected)
				flushRun();

			if (runStart < 0)
			{
				runStart = offset;
				runColumn = column;
				runSelected = selected;
			}

			++column;
			continue;
		}

		flushRun();

		if (selected)
			highlight(column, column + columns);

		if (ch != QChar('\t')) // A tab paints nothing, it only advances to its stop
		{
			if (isSubstituted(ch))
			{
				const QChar substitute = displayCharForNonPrintable(ch);
				drawChars(&substitute, 1, column, selected);
			}
			else
				drawChars(_text.constData() + offset, ch.isHighSurrogate() && offset + 1 < textLength ? 2 : 1, column, selected);
		}

		column += columns;
	}

	flushRun();
}

void CLightningFastViewerWidget::updateLayoutAndScrollBars()
{
	if (_mode == HEX)
		calculateHexLayout();

	const int visibleLines = viewport()->height() / _lineHeight;
	verticalScrollBar()->setRange(0, static_cast<int>(qMax<qsizetype>(0, totalLines() - visibleLines)));
	verticalScrollBar()->setPageStep(visibleLines);
	verticalScrollBar()->setSingleStep(1);

	const qsizetype contentWidth = (_mode == HEX)
		? _asciiStart + _charWidth * _bytesPerLine
		: (_maxLineColumns + Layout::TEXT_HORIZONTAL_MARGIN_CHARS) * _charWidth;

	horizontalScrollBar()->setRange(0, static_cast<int>(qMax<qsizetype>(0, contentWidth - viewport()->width())));
	horizontalScrollBar()->setPageStep(viewport()->width());
	horizontalScrollBar()->setSingleStep(_charWidth);
}

CLightningFastViewerWidget::Region CLightningFastViewerWidget::regionAtPos(const QPoint& pos) const
{
	int x = pos.x() + horizontalScrollBar()->value();

	if (x < _hexStart)
		return REGION_OFFSET;
	if (x < _asciiStart - _charWidth * Layout::HEX_ASCII_SEPARATOR_CHARS)
		return REGION_HEX;
	if (x >= _asciiStart)
		return REGION_ASCII;
	return REGION_NONE;
}

qsizetype CLightningFastViewerWidget::hexPosToOffset(const QPoint& pos) const
{
	if (_data.isEmpty())
		return -1;

	const int line = (pos.y() / _lineHeight) + verticalScrollBar()->value();
	if (line < 0 || line >= totalLines())
		return -1;

	const int x = pos.x() + horizontalScrollBar()->value();
	Region region = regionAtPos(pos);

	qsizetype lineOffset = line * _bytesPerLine;
	int byteInLine = 0;

	if (region == REGION_HEX)
	{
		int relX = x - _hexStart;
		byteInLine = relX / (_charWidth * Layout::HEX_CHARS_PER_BYTE);

		// Account for extra space every 8 bytes
		if (byteInLine > 8)
		{
			relX -= _charWidth * Layout::HEX_MIDDLE_EXTRA_SPACE * ((byteInLine - 1) / 8);
			byteInLine = relX / (_charWidth * Layout::HEX_CHARS_PER_BYTE);
		}

		byteInLine = qBound(0, byteInLine, _bytesPerLine - 1);
	}
	else if (region == REGION_ASCII)
	{
		const int relX = x - _asciiStart;
		byteInLine = relX / _charWidth;
		byteInLine = qBound(0, byteInLine, _bytesPerLine - 1);
	}
	else
	{
		return -1; // Fix #5: Invalid region
	}

	qsizetype offset = lineOffset + byteInLine;
	return qMin(offset, (qsizetype)_data.size() - 1);
}

qsizetype CLightningFastViewerWidget::textPosToOffset(const QPoint& pos) const
{
	const qsizetype line = pos.y() / _lineHeight + verticalScrollBar()->value();
	if (line < 0 || line >= totalLines())
		return -1;

	const qsizetype lineStart = _lineOffsets[line];
	const qsizetype lineEnd = _lineOffsets[line + 1];
	const qsizetype textLength = _text.size();
	const qsizetype targetColumn = qMax<qsizetype>(0, (pos.x() + horizontalScrollBar()->value() - Layout::LEFT_MARGIN_PIXELS) / _charWidth);

	qsizetype column = 0;
	for (qsizetype offset = lineStart; offset < lineEnd; ++offset)
	{
		const int columns = columnsForChar(_text[offset], offset + 1 < textLength ? _text[offset + 1] : QChar(), column);
		if (columns == 0)
			continue;

		if (targetColumn < column + columns)
			return offset;

		column += columns;
	}

	return lineEnd - 1; // Clicked past the end of the line
}

void CLightningFastViewerWidget::ensureVisible(qsizetype offset)
{
	qsizetype line = 0;

	if (_mode == HEX)
	{
		line = offset / _bytesPerLine;
	}
	else
	{
		// Find which line contains this offset
		line = findLineContainingOffset(offset);
		if (line < 0)
			return;
	}

	const int firstVisible = verticalScrollBar()->value();
	const int visibleLines = viewport()->height() / _lineHeight;

	if (line < firstVisible)
	{
		verticalScrollBar()->setValue(line);
	}
	else if (line >= firstVisible + visibleLines)
	{
		verticalScrollBar()->setValue(line - visibleLines + 1);
	}
}

void CLightningFastViewerWidget::copySelection()
{
	if (!_selection.isValid())
		return;

	qsizetype start = _selection.selStart();
	qsizetype end = _selection.selEnd();

	if (_mode == TEXT)
	{
		// Copy selected text
		QString selected = _text.mid(start, end - start + 1);
		QApplication::clipboard()->setText(selected);
		return;
	}

	// Hex mode
	if (_selection.region == REGION_HEX)
	{
		// Copy as hex string
		QString hexStr;
		for (qsizetype i = start; i <= end && i < _data.size(); ++i)
		{
			const unsigned char byte = static_cast<unsigned char>(_data[i]);
			hexStr += QChar(hexChars[byte >> 4]);
			hexStr += QChar(hexChars[byte & 0x0F]);
			if (i < end) hexStr += ' ';
		}
		QApplication::clipboard()->setText(hexStr);
	}
	else if (_selection.region == REGION_ASCII)
	{
		// Copy as ASCII
		QString asciiStr;
		for (qsizetype i = start; i <= end && i < _data.size(); ++i)
		{
			const unsigned char byte = static_cast<unsigned char>(_data[i]);
			asciiStr += (byte >= 32 && byte <= 126) ? QChar(byte) : QChar('.');
		}
		QApplication::clipboard()->setText(asciiStr);
	}
	else
	{
		// Copy raw bytes
		QByteArray selected = _data.mid(start, end - start + 1);
		QApplication::clipboard()->setText(QString::fromLatin1(selected.toHex(' ')));
	}
}

void CLightningFastViewerWidget::selectAll()
{
	// Fix #4: Guard against empty data
	if (_mode == HEX && _data.isEmpty())
		return;
	if (_mode == TEXT && _text.isEmpty())
		return;

	_selection.start = 0;
	if (_mode == HEX)
		_selection.end = _data.size() - 1;
	else
		_selection.end = _text.size() - 1;
	_selection.region = (_mode == HEX) ? REGION_HEX : REGION_ASCII;
	viewport()->update();
}

bool CLightningFastViewerWidget::isSelected(qsizetype offset) const
{
	return _selection.isValid() &&
		offset >= _selection.selStart() &&
		offset <= _selection.selEnd();
}

int CLightningFastViewerWidget::columnsForChar(QChar ch, QChar next, qsizetype column) const
{
	const char16_t code = ch.unicode();
	if (code >= 0x20 && code < 0x7F) [[likely]]
		return 1;

	switch (code)
	{
	case u'\n':
		return 0;
	case u'\r':
		return next == u'\n' ? 0 : 1; // A CR before LF is part of the line terminator, a lone CR is content
	case u'\t':
		return static_cast<int>(_tabWidth - column % _tabWidth);
	default:
		break;
	}

	if (code < 0x20 || code == 0x7F)
		return 1; // Painted as a stand-in glyph

	return columnsForNonAsciiChar(ch);
}

int CLightningFastViewerWidget::columnsForNonAsciiChar(QChar ch) const
{
	if (ch.isLowSurrogate())
		return 0; // The pair occupies the columns claimed by its high surrogate
	if (ch.isHighSurrogate())
		return 2; // Measuring would require the whole pair; non-BMP characters are double width in practice

	if (_charColumns.empty())
		_charColumns.resize(0x10000, 0);

	uint8_t& columns = _charColumns[ch.unicode()];
	if (columns == 0)
	{
		columns = isSubstituted(ch)
			? uint8_t{ 1 }
			: static_cast<uint8_t>(qBound(1, (_fontMetrics.horizontalAdvance(ch) + _charWidth / 2) / _charWidth, 255));
	}

	return columns;
}

void CLightningFastViewerWidget::rebuildLineIndexIfNeeded()
{
	if (_mode != TEXT || !_geometrySettled)
		return;

	const qsizetype maxColumns = _wordWrap
		? qMax<qsizetype>(1, (viewport()->width() - _charWidth * Layout::TEXT_HORIZONTAL_MARGIN_CHARS) / _charWidth)
		: Layout::NO_WRAP_COLUMNS;

	if (maxColumns == _wrappedForMaxColumns)
		return;

	_wrappedForMaxColumns = maxColumns;
	_lineOffsets.clear();
	_maxLineColumns = 0;

	const qsizetype textLength = _text.length();
	if (textLength == 0)
		return;

	qsizetype lineStart = 0;
	qsizetype column = 0;           // Columns occupied by the current line so far
	qsizetype breakPos = -1;        // Last position the current line may be broken after; -1 when there is none
	qsizetype columnsBeforeBreak = 0; // Excludes the break character itself, which is whitespace hanging past the margin

	_lineOffsets.push_back(0);

	const auto endLine = [&](qsizetype endedLineColumns, qsizetype nextLineStart) {
		_maxLineColumns = qMax(_maxLineColumns, endedLineColumns);
		_lineOffsets.push_back(nextLineStart);
		lineStart = nextLineStart;
		column = 0;
		breakPos = -1;
	};

	const QChar* const chars = _text.constData(); // QString::operator[] is the detaching overload here, and setText leaves _text shared

	for (qsizetype i = 0; i < textLength; ++i)
	{
		const QChar ch = chars[i];
		const int columns = columnsForChar(ch, i + 1 < textLength ? chars[i + 1] : QChar(), column);
		column += columns;

		if (ch == u'\n')
		{
			endLine(column, i + 1);
			continue;
		}

		if (ch == u' ' || ch == u'\t')
		{
			breakPos = i;
			columnsBeforeBreak = column - columns;
		}

		if (column > maxColumns && i > lineStart)
		{
			// Both branches rewind: a tab's width depends on the column it starts at, so the carried-over text is measured again against the new line
			if (breakPos > lineStart)
			{
				i = breakPos; // Must precede endLine, which clears breakPos
				endLine(columnsBeforeBreak, i + 1); // The character broken after stays on the line it ends
			}
			else
			{
				endLine(column - columns, i);
				--i;
			}
		}
	}

	_maxLineColumns = qMax(_maxLineColumns, column);
	if (_lineOffsets.back() != textLength)
		_lineOffsets.push_back(textLength);
}

void CLightningFastViewerWidget::updateFontMetrics()
{
	_fontMetrics = QFontMetrics(font());
	_lineHeight = _fontMetrics.height();
	_charWidth = _fontMetrics.horizontalAdvance('0');
	assert_r(_lineHeight > 0 && _charWidth > 0);

	_charColumns.clear();
	_wrappedForMaxColumns = -1; // Measured column counts, and the wrap width itself, follow the font
}

qsizetype CLightningFastViewerWidget::findLineContainingOffset(qsizetype offset) const
{
	if (offset < 0 || offset >= _text.size())
		return -1;

	// The trailing sentinel guarantees a hit for any offset within the text
	const auto line = std::upper_bound(_lineOffsets.begin(), _lineOffsets.end(), offset);
	return line - _lineOffsets.begin() - 1;
}

void CLightningFastViewerWidget::moveCursor(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode)
{
	const qsizetype dataSize = (_mode == HEX) ? _data.size() : _text.size();
	const qsizetype targetOffset = (operation == QTextCursor::Start) ? 0 : qMax(qsizetype(0), dataSize - 1);

	// The inclusive end cannot express an empty selection, so collapsing means clearing: a search then starts at the target, not past it.
	if (mode == QTextCursor::MoveAnchor)
		_selection = Selection();
	else
	{
		if (!_selection.isValid())
			_selection.start = targetOffset;
		_selection.end = targetOffset;
		_selection.region = REGION_ASCII;
	}

	ensureVisible(targetOffset);
	viewport()->update();
}

// Finds the match nearest to 'from' in the given direction that satisfies the whole-word constraint.
// Matcher signature: (qsizetype from, bool backward) -> pair<qsizetype /*pos*/, qsizetype /*len*/>
// A negative 'from' means nothing is left to search: QString::lastIndexOf reads it as "start at the end".
// Returns {-1, 0} when there is no such match.
template <typename Matcher>
static std::pair<qsizetype, qsizetype> searchWholeWordFiltered(
	const QString& haystack, qsizetype from, bool backward, bool wholeWords, Matcher matcher)
{
	const auto isWholeWord = [&](qsizetype pos, qsizetype len) {
		const bool left = (pos == 0) || !haystack[pos - 1].isLetterOrNumber();
		const bool right = (pos + len >= haystack.size()) || !haystack[pos + len].isLetterOrNumber();
		return left && right;
	};

	while (from >= 0)
	{
		const auto [matchPos, matchLen] = matcher(from, backward);
		if (matchPos < 0)
			break;

		if (!wholeWords || isWholeWord(matchPos, matchLen))
			return { matchPos, matchLen };

		from = backward ? matchPos - 1 : matchPos + 1;
	}

	return { -1, 0 };
}

qsizetype CLightningFastViewerWidget::searchStartOffset(bool backward, qsizetype haystackSize) const
{
	if (!_selection.isValid())
		return backward ? haystackSize - 1 : 0;

	return backward ? _selection.selStart() - 1 : _selection.selEnd() + 1;
}

bool CLightningFastViewerWidget::find(const QString& exp, QTextDocument::FindFlags options)
{
	if (exp.isEmpty())
		return false;

	const bool backward = options & QTextDocument::FindBackward;
	const bool wholeWords = options & QTextDocument::FindWholeWords;
	const Qt::CaseSensitivity cs = (options & QTextDocument::FindCaseSensitively) ? Qt::CaseSensitive : Qt::CaseInsensitive;

	const QString& haystack = (_mode == TEXT) ? _text : QString::fromLatin1(_data);
	if (haystack.isEmpty())
		return false;

	auto matcher = [&](qsizetype from, bool bwd) -> std::pair<qsizetype, qsizetype> {
		const qsizetype pos = bwd ? haystack.lastIndexOf(exp, from, cs) : haystack.indexOf(exp, from, cs);
		return { pos, exp.size() };
	};

	const auto [matchPos, matchLen] = searchWholeWordFiltered(haystack, searchStartOffset(backward, haystack.size()), backward, wholeWords, matcher);
	if (matchPos < 0)
		return false;

	_selection.start = matchPos;
	_selection.end = matchPos + matchLen - 1;
	_selection.region = REGION_ASCII;
	ensureVisible(matchPos);
	viewport()->update();
	return true;
}

bool CLightningFastViewerWidget::find(const QRegularExpression& exp, QTextDocument::FindFlags options)
{
	if (!exp.isValid() || exp.pattern().isEmpty())
		return false;

	const bool backward = options & QTextDocument::FindBackward;
	const bool wholeWords = options & QTextDocument::FindWholeWords;

	// Enforce the case-sensitivity flag by overriding it in the regex
	QRegularExpression rx = exp;
	if (options & QTextDocument::FindCaseSensitively)
		rx.setPatternOptions(rx.patternOptions() & ~QRegularExpression::CaseInsensitiveOption);
	else
		rx.setPatternOptions(rx.patternOptions() | QRegularExpression::CaseInsensitiveOption);

	const QString& haystack = (_mode == TEXT) ? _text : QString::fromLatin1(_data);
	if (haystack.isEmpty())
		return false;

	auto matcher = [&](qsizetype from, bool bwd) -> std::pair<qsizetype, qsizetype> {
		if (!bwd)
		{
			const QRegularExpressionMatch m = rx.match(haystack, from);
			return m.hasMatch() ? std::pair{ m.capturedStart(), m.capturedLength() } : std::pair{ qsizetype(-1), qsizetype(0) };
		}
		else
		{
			// Qt has no lastIndexOf for regex; iterate forward and keep the rightmost match <= from
			std::pair<qsizetype, qsizetype> last{ -1, 0 };
			for (auto it = rx.globalMatch(haystack); it.hasNext(); )
			{
				const QRegularExpressionMatch m = it.next();
				if (m.capturedStart() <= from)
					last = { m.capturedStart(), m.capturedLength() };
				else
					break;
			}
			return last;
		}
	};

	const auto [matchPos, matchLen] = searchWholeWordFiltered(haystack, searchStartOffset(backward, haystack.size()), backward, wholeWords, matcher);
	if (matchPos < 0)
		return false;

	_selection.start = matchPos;
	_selection.end = matchPos + matchLen - 1;
	_selection.region = REGION_ASCII;
	ensureVisible(matchPos);
	viewport()->update();
	return true;
}

int CLightningFastViewerWidget::selectionStart() const
{
	return _selection.isValid() ? (int)_selection.selStart() : -1;
}

void CLightningFastViewerWidget::updateCursorShape(const QPoint& pos)
{
	bool overText = false;

	if (_mode == HEX)
	{
		Region region = regionAtPos(pos);
		overText = (region == REGION_HEX || region == REGION_ASCII);
	}
	else // TEXT mode
	{
		qsizetype offset = textPosToOffset(pos);
		overText = (offset >= 0);
	}

	viewport()->setCursor(overText ? Qt::IBeamCursor : Qt::ArrowCursor);
}
