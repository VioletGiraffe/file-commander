#include "clightningfastviewer.h"
#include "system/ctimeelapsed.h"
#include "utility/on_scope_exit.hpp"
#include "assert/advanced_assert.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QtMath>

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
	_selection = Selection();
	clearWrappingData();

	if (_initialized) // The initial wrapping on first show is handled in resizeEvent, but for subsequent text changes it is needed here
		wrapTextIfNeeded();

	updateScrollBarsAndHexLayout();
	viewport()->update();
}

void CLightningFastViewerWidget::setText(const QString& text)
{
	_mode = TEXT;
	_text = text;

	_data.clear();
	_selection = Selection();
	clearWrappingData();

	if (_initialized) // The initial wrapping on first show is handled in resizeEvent, but for subsequent text changes it is needed here
		wrapTextIfNeeded();

	updateScrollBarsAndHexLayout();
	viewport()->update();
}

void CLightningFastViewerWidget::setWordWrap(bool enabled)
{
	if (_wordWrap != enabled)
	{
		_wordWrap = enabled;
		clearWrappingData();
		wrapTextIfNeeded();
		updateScrollBarsAndHexLayout();
		viewport()->update();
	}
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
			drawTextLine(painter, static_cast<int>(line), y, _fontMetrics);
		}
		y += _lineHeight;
	}
}

void CLightningFastViewerWidget::resizeEvent(QResizeEvent* event)
{
	QAbstractScrollArea::resizeEvent(event);
	_initialized = true;
	wrapTextIfNeeded();
	updateScrollBarsAndHexLayout();
}

bool CLightningFastViewerWidget::event(QEvent* event)
{
	switch (const auto type = event->type(); type)
	{
	case QEvent::FontChange:
	{
		updateFontMetrics();
		if (_initialized) // This will already be handled in resizeEvent on first show, but in case of subsequent font changes we need to re-wrap and update
		{
			wrapTextIfNeeded();
			updateScrollBarsAndHexLayout();
			viewport()->update();
		}
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
			int currentLine = findLineContainingOffset(cursorPos);
			if (currentLine > 0)
			{
				// Try to maintain column position
				int colInLine = cursorPos - _lineOffsets[currentLine];
				newPos = qMin((qsizetype)(_lineOffsets[currentLine - 1] + colInLine),
					(qsizetype)(_lineOffsets[currentLine - 1] + _lineLengths[currentLine - 1] - 1));
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
			int currentLine = findLineContainingOffset(cursorPos);
			if (currentLine >= 0 && currentLine < static_cast<int>(_lineOffsets.size()) - 1)
			{
				// Try to maintain column position
				int colInLine = cursorPos - _lineOffsets[currentLine];
				newPos = qMin((qsizetype)(_lineOffsets[currentLine + 1] + colInLine),
					(qsizetype)(_lineOffsets[currentLine + 1] + _lineLengths[currentLine + 1] - 1));
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
			int currentLine = findLineContainingOffset(cursorPos);
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
			int currentLine = findLineContainingOffset(cursorPos);
			if (currentLine >= 0)
			{
				newPos = qMin(maxOffset, (qsizetype)(_lineOffsets[currentLine] + _lineLengths[currentLine] - 1));
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

int CLightningFastViewerWidget::totalLines() const
{
	if (_mode == HEX)
	{
		if (_data.isEmpty())
			return 0;
		return static_cast<int>((_data.size() + (qsizetype)_bytesPerLine - 1) / (qsizetype)_bytesPerLine);
	}
	else
	{
		return static_cast<int>(_lineOffsets.size());
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

void CLightningFastViewerWidget::drawTextLine(QPainter& painter, int lineIndex, int y, const QFontMetrics& fm)
{
	if (lineIndex < 0 || lineIndex >= static_cast<int>(_lineOffsets.size()))
		return;

	const int hScroll = horizontalScrollBar()->value();

	const int lineStart = _lineOffsets[lineIndex];
	const int lineLen = _lineLengths[lineIndex];
	QString lineText = _text.mid(lineStart, lineLen);

	// Draw the line character by character to handle selection
	int x = Layout::LEFT_MARGIN_PIXELS - hScroll;
	for (int i = 0; i < lineText.length(); ++i)
	{
		qsizetype charOffset = lineStart + i;

		if (isSelected(charOffset))
		{
			int charWidth = fm.horizontalAdvance(lineText[i]);
			QRect selRect(x, y, charWidth, _lineHeight);
			painter.fillRect(selRect, palette().highlight());
			painter.setPen(palette().highlightedText().color());
		}
		else
		{
			painter.setPen(palette().color(QPalette::Text));
		}

		painter.drawText(x, y + fm.ascent(), lineText[i]);
		x += fm.horizontalAdvance(lineText[i]);
	}
}

void CLightningFastViewerWidget::updateScrollBarsAndHexLayout()
{
	if (_mode == HEX)
		calculateHexLayout();

	const int visibleLines = viewport()->height() / _lineHeight;
	verticalScrollBar()->setRange(0, qMax(0, totalLines() - visibleLines));
	verticalScrollBar()->setPageStep(visibleLines);
	verticalScrollBar()->setSingleStep(1);

	// Horizontal scrollbar
	if (_mode == HEX)
	{
		const int totalWidth = _asciiStart + _charWidth * _bytesPerLine;
		horizontalScrollBar()->setRange(0, qMax(0, totalWidth - viewport()->width()));
	}
	else
	{
		// In text mode, estimate max line width based on character count
		// For monospace fonts, this is much faster than measuring each line
		int maxChars = 0;
		for (size_t i = 0; i < _lineOffsets.size(); ++i)
		{
			const int lineChars = _lineLengths[i];

			// Quick scan for tabs in this line to adjust estimate
			int tabCount = 0;
			for (int j = _lineOffsets[i], end = j + lineChars; j < end; ++j)
			{
				if (_text[j] == '\t') [[unlikely]]
					++tabCount;
			}

			// Estimate: each tab expands based on measured _tabWidthInChars
			const int estimatedChars = lineChars + (tabCount * (_tabWidthInChars - 1));
			maxChars = qMax(maxChars, estimatedChars);
		}

		const int maxWidth = maxChars * _charWidth;
		horizontalScrollBar()->setRange(0, qMax(0, maxWidth - viewport()->width() + _charWidth * Layout::TEXT_HORIZONTAL_MARGIN_CHARS));
	}
	horizontalScrollBar()->setPageStep(viewport()->width());
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
	if (_text.isEmpty() || _lineOffsets.empty())
		return -1;

	int line = (pos.y() / _lineHeight) + verticalScrollBar()->value();
	if (line < 0 || line >= static_cast<int>(_lineOffsets.size()))
		return -1;

	int x = pos.x() + horizontalScrollBar()->value();
	int lineStart = _lineOffsets[line];
	int lineLen = _lineLengths[line];
	QString lineText = _text.mid(lineStart, lineLen);

	// Find character at position
	int currentX = Layout::LEFT_MARGIN_PIXELS;

	for (int i = 0; i < lineText.length(); ++i)
	{
		int charWidth = _fontMetrics.horizontalAdvance(lineText[i]);
		if (x < currentX + charWidth / 2)
			return lineStart + i;
		currentX += charWidth;
	}

	// Clicked past end of line
	return qMin((qsizetype)(lineStart + lineLen - 1), (qsizetype)(_text.size() - 1));
}

void CLightningFastViewerWidget::ensureVisible(qsizetype offset)
{
	int line = 0;

	if (_mode == HEX)
	{
		line = static_cast<int>(offset / _bytesPerLine);
	}
	else
	{
		// Find which line contains this offset
		line = findLineContainingOffset(offset);
		if (line < 0)
			return;
	}

	int firstVisible = verticalScrollBar()->value();
	int visibleLines = viewport()->height() / _lineHeight;

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

void CLightningFastViewerWidget::wrapTextIfNeeded()
{
	if (_mode != TEXT)
		return;
	else if (_text.isEmpty())
		return;

	CTimeElapsed timer(true);
	EXEC_ON_SCOPE_EXIT([&] {
		const auto elapsed = timer.elapsed();
		if (elapsed > 2)
			qInfo() << "wrap text:" << timer.elapsed();
		});

	if (!_wordWrap && _lineOffsets.empty())
	{
		clearWrappingData();

		// No wrapping - split on newlines only
		int start = 0;
		int newlinePos;

		while ((newlinePos = _text.indexOf('\n', start)) != -1)
		{
			_lineOffsets.push_back(start);
			_lineLengths.push_back(newlinePos - start + 1); // Include the newline
			start = newlinePos + 1;
		}

		// Last line (or entire text if no newlines)
		if (start < _text.length())
		{
			_lineOffsets.push_back(start);
			_lineLengths.push_back(_text.length() - start);
		}
		return;
	}

	// Word wrapping enabled
	const size_t hashKey = qHash(viewport()->width()) ^ qHash(_charWidth);
	if (hashKey == _wordWrapParamsHash)
		return;

	clearWrappingData();
	_wordWrapParamsHash = hashKey;

	const int maxCharsPerLine = (viewport()->width() - _charWidth * Layout::TEXT_HORIZONTAL_MARGIN_CHARS) / _charWidth;
	if (maxCharsPerLine <= 0)
		return; // Guard against too small viewport

	int currentLineStart = 0;
	int currentCharCount = 0; // Character count in current line
	int lastBreakPos = -1; // Last position where we could break
	int lastBreakCharCount = 0; // Character count at last break position

	for (qsizetype i = 0, n = _text.length(); i < n; ++i)
	{
		QChar ch = _text[i];

		// Handle explicit newlines
		if (ch == '\n')
		{
			_lineOffsets.push_back(currentLineStart);
			_lineLengths.push_back(i - currentLineStart + 1);
			currentLineStart = i + 1;
			currentCharCount = 0;
			lastBreakPos = -1;
			lastBreakCharCount = 0;
			continue;
		}

		// Calculate character width in monospace units
		int charWidth = 1; // Default for most characters
		if (ch == '\t')
		{
			// Tab width: advance to next multiple of _tabWidthInChars
			int nextTabStop = ((currentCharCount / _tabWidthInChars) + 1) * _tabWidthInChars;
			charWidth = nextTabStop - currentCharCount;
		}
		else if (ch.unicode() < 32 || ch.category() == QChar::Other_Control)
		{
			// Control characters take 0 width
			charWidth = 0;
		}

		currentCharCount += charWidth;

		// Mark break opportunities (spaces)
		if (ch.isSpace() && ch != '\t')
		{
			lastBreakPos = i;
			lastBreakCharCount = currentCharCount;
		}

		// Line is too long
		if (currentCharCount > maxCharsPerLine && i > currentLineStart)
		{
			int breakPos;

			// Try to break at last space
			if (lastBreakPos > currentLineStart)
			{
				breakPos = lastBreakPos;
				_lineOffsets.push_back(currentLineStart);
				_lineLengths.push_back(breakPos - currentLineStart);

				// Start new line after the space and use the already tracked count
				currentLineStart = breakPos + 1;
				currentCharCount -= lastBreakCharCount;
			}
			else
			{
				// No space found, break at current position
				breakPos = i;
				_lineOffsets.push_back(currentLineStart);
				_lineLengths.push_back(breakPos - currentLineStart);

				currentLineStart = breakPos;
				currentCharCount = charWidth;
			}

			lastBreakPos = -1;
			lastBreakCharCount = 0;
		}
	}

	// Last line
	if (currentLineStart < _text.length())
	{
		_lineOffsets.push_back(currentLineStart);
		_lineLengths.push_back(_text.length() - currentLineStart);
	}
}

void CLightningFastViewerWidget::clearWrappingData()
{
	_lineOffsets.clear();
	_lineLengths.clear();
	_wordWrapParamsHash = 0;
}

void CLightningFastViewerWidget::updateFontMetrics()
{
	_fontMetrics = QFontMetrics(font());
	_lineHeight = _fontMetrics.height();
	_charWidth = _fontMetrics.horizontalAdvance('0');

	// Empirically measure tab width
	const int widthWithTab = _fontMetrics.horizontalAdvance("X\tY");
	const int widthWithoutTab = _fontMetrics.horizontalAdvance("XY");
	const int tabWidth = widthWithTab - widthWithoutTab;
	_tabWidthInChars = qMax(1, tabWidth / _charWidth); // Ensure at least 1
}

int CLightningFastViewerWidget::findLineContainingOffset(qsizetype offset) const
{
	for (size_t i = 0; i < _lineOffsets.size(); ++i)
	{
		if (_lineOffsets[i] <= offset && offset < _lineOffsets[i] + _lineLengths[i])
		{
			return static_cast<int>(i);
		}
	}
	return -1;
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
