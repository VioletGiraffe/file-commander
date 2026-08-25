#include "clightningfastviewer.h"
#include "assert/advanced_assert.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>

#include <algorithm>
#include <limits>

static constexpr char hexChars[] = "0123456789ABCDEF";
static constexpr int autoScrollIntervalMs = 50;

namespace Layout {
	static constexpr int MIN_OFFSET_DIGITS = 4;
	static constexpr int OFFSET_SUFFIX_CHARS = 2; // ": "
	static constexpr int HEX_CHARS_PER_BYTE = 3; // "XX "
	static constexpr int HEX_BYTES_PER_GROUP = 8;
	static constexpr int HEX_MIDDLE_EXTRA_SPACE = 1; // Extra space after each group
	static constexpr int HEX_GROUP_CHARS = HEX_BYTES_PER_GROUP * HEX_CHARS_PER_BYTE + HEX_MIDDLE_EXTRA_SPACE;
	static constexpr int HEX_ASCII_SEPARATOR_CHARS = 2; // The separator is technically " | ",
	// but we get the space on the left by default due to it being included with every byte via HEX_CHARS_PER_BYTE
	static constexpr int LEFT_MARGIN_PIXELS = 2;
	static constexpr int TEXT_HORIZONTAL_MARGIN_CHARS = 2;

	// Line width used when word wrap is off: never reached, and constant, so resizing cannot invalidate the index.
	static constexpr qsizetype NO_WRAP_COLUMNS = std::numeric_limits<qsizetype>::max();
}

// The CP437 glyphs for the C0 controls, as a DOS-era dump showed them. Code points rather than literal glyphs: the sources are ASCII-only and MSVC is not passed /utf-8.
// Slot 0 is ours, not CP437's: CP437 draws NUL as a blank, which would be indistinguishable from a space.
static constexpr char16_t cp437ControlGlyphs[0x20] = {
	0x2205, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, // empty set, smilies, card suits, bullet
	0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C, // circles, gender signs, notes, sun
	0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8, // triangles, arrows, pilcrow, section, bar
	0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC  // arrows, right angle, triangles
};

static constexpr char16_t cp437DeleteGlyph = 0x2302;   // house

// The CP437 glyphs for 0x80 to 0xFF, indexed by byte minus 0x80. The last slot is ours: CP437 puts a no-break space there, which would read as a blank.
static constexpr char16_t cp437HighGlyphs[0x80] = {
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, // 0x80
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, // 0x90
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, // 0xA0
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, // 0xB0, shading and box drawing
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, // 0xC0
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, // 0xD0
	0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, // 0xE0, Greek and math
	0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x25A9  // 0xF0
};
static constexpr char16_t openBoxGlyph = 0x2423;       // marks a no-break space
static constexpr char16_t invisibleMarkerGlyph = 0x25AF; // white vertical rectangle
static constexpr char16_t fallbackGlyph = '.';

namespace {

// Byte classes carry their own colours: QPalette has no categorical colour set to borrow, and the hues have to hold up on either background.
struct ByteColors
{
	QColor null;
	QColor whitespace;
	QColor printable;
	QColor control;
	QColor nonAscii;
	QColor filler;

	[[nodiscard]] const QColor& forByte(uint8_t byte) const
	{
		if (byte == 0x00)
			return null;
		if (byte == 0xFF)
			return filler;
		if (byte == 0x20 || (byte >= 0x09 && byte <= 0x0D))
			return whitespace;
		if (byte < 0x20 || byte == 0x7F)
			return control;
		if (byte >= 0x80)
			return nonAscii;

		return printable;
	}
};

ByteColors byteColors(const QPalette& palette)
{
	const bool darkBackground = palette.color(QPalette::Base).lightness() < 128;

	return {
		.null = palette.color(QPalette::Disabled, QPalette::Text),
		.whitespace = darkBackground ? QColor(0x7F, 0xD0, 0x7F) : QColor(0x18, 0x78, 0x18),
		.printable = palette.color(QPalette::Text),
		.control = darkBackground ? QColor(0x6F, 0xD0, 0xD0) : QColor(0x10, 0x68, 0x68),
		.nonAscii = darkBackground ? QColor(0xD8, 0xB0, 0x60) : QColor(0x8A, 0x5A, 0x00),
		.filler = darkBackground ? QColor(0xD0, 0x80, 0xC0) : QColor(0x9A, 0x0F, 0x7A)
	};
}

} // namespace

inline constexpr bool isPrintableAscii(char16_t code)
{
	return code >= 0x20 && code < 0x7F;
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

QFont CLightningFastViewerWidget::preferredFixedFont()
{
	// The system fixed font is a poor default for a data view - Courier New on Windows, covering neither the stand-in glyphs nor much else. Only the family is chosen here; the size resolves from the parent as usual.
#if defined _WIN32
	static const char* const preferredFamilies[] = { "Cascadia Mono", "Consolas" };
#elif defined __APPLE__
	static const char* const preferredFamilies[] = { "SF Mono", "Menlo", "Monaco" };
#else
	static const char* const preferredFamilies[] = { "DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono" };
#endif

	for (const char* const family : preferredFamilies)
	{
		const QString familyName = QString::fromLatin1(family);
		const QFont candidate{ familyName };
		const QFontInfo info{ candidate };

		// QFont substitutes silently for a family that is not installed, and QFontInfo reports what was actually resolved
		if (info.family().compare(familyName, Qt::CaseInsensitive) == 0 && info.fixedPitch())
			return candidate;
	}

	return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

CLightningFastViewerWidget::CLightningFastViewerWidget(QWidget* parent)
	: QAbstractScrollArea(parent), _fontMetrics(font())
{
	setFont(preferredFixedFont());
	updateFontMetrics(); // Not redundant with the FontChange handler: setFont skips the event when the font already equals the new one

	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	viewport()->setMouseTracking(true);
}

void CLightningFastViewerWidget::setData(const QByteArray& bytes)
{
	_mode = Mode::Hex;
	_data = bytes;
	_text.clear();

	contentChanged();
}

void CLightningFastViewerWidget::setText(const QString& text)
{
	_mode = Mode::Text;
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
	endDrag(); // The selection a drag was building goes away with the content

	// Keyboard navigation never sets a region, so copying has to find a usable one from the start
	_selection = Selection{ .region = (_mode == Mode::Hex) ? Region::Hex : Region::Ascii };
	_hexSearchText.clear();
	_foldedData.clear();
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

	const qsizetype firstLine = verticalScrollBar()->value();
	const qsizetype lastLine = qMin(firstLine + visibleLines() + 2, totalLines());

	int y = 0;
	for (qsizetype line = firstLine; line < lastLine; ++line)
	{
		if (_mode == Mode::Hex)
		{
			const qsizetype offset = line * _bytesPerLine;
			drawHexLine(painter, offset, y);
		}
		else
		{
			drawTextLine(painter, line, y);
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
		assert_r(QFontInfo{ font() }.fixedPitch());
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
		const Region region = (_mode == Mode::Hex) ? regionAtPos(event->pos()) : Region::Ascii;
		const qsizetype offset = (_mode == Mode::Hex) ? hexPosToOffset(event->pos(), region) : textPosToOffset(event->pos());
		if (offset >= 0)
		{
			// A click selects the cell, and anchors the drag that may follow
			_selection.selectRange(offset, 1, region);
			_dragPos = event->pos();
			_dragging = true;
			viewport()->update();
		}
	}
}

void CLightningFastViewerWidget::mouseMoveEvent(QMouseEvent* event)
{
	updateCursorShape(event->pos());

	if (!_dragging)
		return;

	if (!(event->buttons() & Qt::LeftButton)) // The grab can end without a release event, e.g. when a modal dialog opens over the drag
	{
		endDrag();
		return;
	}

	_dragPos = event->pos();
	extendSelectionToDragPos();

	// A stationary pointer produces no further move events, so scrolling past an edge has to come from a timer
	if (viewport()->rect().contains(_dragPos))
		stopAutoScroll();
	else if (_autoScrollTimer == 0)
		_autoScrollTimer = startTimer(autoScrollIntervalMs);
}

void CLightningFastViewerWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
		endDrag();
}

void CLightningFastViewerWidget::contextMenuEvent(QContextMenuEvent* event)
{
	QMenu menu(this);

	const auto addCopyAction = [&](const QString& text, Region format) {
		menu.addAction(text, this, [this, format] { copySelection(format); })->setEnabled(_selection.hasSelection());
	};

	// Both columns show the same bytes, so either rendering is worth offering whichever one the selection was made in
	if (_mode == Mode::Hex)
	{
		addCopyAction(tr("Copy as hex"), Region::Hex);
		addCopyAction(tr("Copy as text"), Region::Ascii);
	}
	else
		addCopyAction(tr("Copy"), Region::Ascii);

	menu.addSeparator();
	menu.addAction(tr("Select all"), this, [this] { selectAll(); });

	menu.exec(event->globalPos());
}

void CLightningFastViewerWidget::timerEvent(QTimerEvent* event)
{
	if (event->timerId() != _autoScrollTimer)
	{
		QAbstractScrollArea::timerEvent(event);
		return;
	}

	// A stationary pointer sends no move event, so a grab lost without a release would scroll on unchecked
	if (!(QApplication::mouseButtons() & Qt::LeftButton))
	{
		endDrag();
		return;
	}

	autoScroll();
	extendSelectionToDragPos();
}

void CLightningFastViewerWidget::extendSelectionToDragPos()
{
	// The hit test needs a position inside the viewport: clamping is what makes a drag past an edge reach the row or column at that edge
	const QRect rect = viewport()->rect();
	const QPoint pos{ qBound(rect.left(), _dragPos.x(), rect.right()), qBound(rect.top(), _dragPos.y(), rect.bottom()) };

	const qsizetype offset = (_mode == Mode::Hex) ? hexPosToOffset(pos, _selection.region) : textPosToOffset(pos);
	if (offset < 0 || offset == _selection.cursor)
		return;

	_selection.extendTo(offset);
	viewport()->update();
}

// Cells to scroll per tick: none while pos is within [min, max], one just outside it, more the further out, never more than a page
static int autoScrollCells(int pos, int min, int max, int cellSize, int cellsPerPage)
{
	if (pos < min)
		return -qMin(1 + (min - pos) / cellSize, cellsPerPage);
	if (pos > max)
		return qMin(1 + (pos - max) / cellSize, cellsPerPage);

	return 0;
}

void CLightningFastViewerWidget::autoScroll()
{
	const QRect rect = viewport()->rect();

	verticalScrollBar()->setValue(verticalScrollBar()->value()
		+ autoScrollCells(_dragPos.y(), rect.top(), rect.bottom(), _lineHeight, visibleLines()));
	horizontalScrollBar()->setValue(horizontalScrollBar()->value()
		+ autoScrollCells(_dragPos.x(), rect.left(), rect.right(), _charWidth, rect.width() / _charWidth) * _charWidth);
}

void CLightningFastViewerWidget::stopAutoScroll()
{
	if (_autoScrollTimer == 0)
		return;

	killTimer(_autoScrollTimer);
	_autoScrollTimer = 0;
}

void CLightningFastViewerWidget::endDrag()
{
	_dragging = false;
	stopAutoScroll();
}

void CLightningFastViewerWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		if (_mode == Mode::Hex)
		{
			const Region region = regionAtPos(event->pos());
			const qsizetype offset = hexPosToOffset(event->pos(), region);
			if (offset >= 0 && offset < _data.size())
			{
				const qsizetype lineStart = (offset / _bytesPerLine) * _bytesPerLine;
				_selection.selectRange(lineStart, qMin(_bytesPerLine, _data.size() - lineStart), region);
				_dragPos = event->pos();
				_dragging = true;
				viewport()->update();
			}
		}
		else
		{
			const qsizetype offset = textPosToOffset(event->pos());
			if (offset >= 0 && offset < _text.size())
			{
				const QChar* const chars = _text.constData(); // QString::operator[] is the detaching overload here

				qsizetype start = offset;
				qsizetype end = offset + 1;

				while (start > 0 && chars[start - 1].isLetterOrNumber())
					--start;
				while (end < _text.size() && chars[end].isLetterOrNumber())
					++end;

				_selection.selectRange(start, end - start, Region::Ascii);
				_dragPos = event->pos();
				_dragging = true;
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
		copySelection(_selection.region);
		return;
	}

	if (ctrlPressed && event->key() == Qt::Key_A)
	{
		selectAll();
		return;
	}

	const qsizetype maxOffset = (_mode == Mode::Hex) ? _data.size() - 1 : _text.size() - 1;
	if (maxOffset < 0) // Empty content
	{
		QAbstractScrollArea::keyPressEvent(event);
		return;
	}

	const qsizetype cursorPos = _selection.hasCursor() ? _selection.cursor : 0;
	qsizetype newPos = cursorPos;
	qsizetype stickyColumn = -1; // Only a vertical move sets this, carrying the cursor's column to the line it lands on

	// Vertical movement follows the display column: a tab or a wide character makes it differ from the offset within the line
	const auto verticalMove = [&](qsizetype lineDelta) {
		if (_mode == Mode::Hex)
			return qBound(qsizetype(0), cursorPos + lineDelta * _bytesPerLine, maxOffset);

		stickyColumn = _selection.desiredColumn;
		return offsetLinesAway(cursorPos, lineDelta, stickyColumn);
	};

	switch (event->key())
	{
	case Qt::Key_Left:
		newPos = qMax(qsizetype(0), cursorPos - 1);
		break;
	case Qt::Key_Right:
		newPos = qMin(maxOffset, cursorPos + 1);
		break;
	case Qt::Key_Up:
		newPos = verticalMove(-1);
		break;
	case Qt::Key_Down:
		newPos = verticalMove(1);
		break;
	case Qt::Key_PageUp:
		newPos = verticalMove(-visibleLines());
		break;
	case Qt::Key_PageDown:
		newPos = verticalMove(visibleLines());
		break;
	case Qt::Key_Home:
		if (ctrlPressed)
		{
			newPos = 0;
		}
		else if (_mode == Mode::Hex)
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
		else if (_mode == Mode::Hex)
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
			_selection.extendTo(newPos, stickyColumn);
		else
			_selection.placeCursor(newPos, stickyColumn);

		ensureVisible(newPos);
		viewport()->update();
	}
}

qsizetype CLightningFastViewerWidget::totalLines() const
{
	if (_mode == Mode::Hex)
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

int CLightningFastViewerWidget::visibleLines() const
{
	return viewport()->height() / _lineHeight;
}

void CLightningFastViewerWidget::calculateHexLayout()
{
	// Sized for the last byte's offset rather than the last line's: _bytesPerLine is derived from this count
	int digits = 1;
	for (qsizetype largestOffset = _data.size() - 1; largestOffset >= 10; largestOffset /= 10)
		++digits;

	_nDigits = qMax(Layout::MIN_OFFSET_DIGITS, digits);

	const int viewportWidth = viewport()->width();
	// The minimum stands even when it overflows the viewport: the horizontal scroll bar covers the excess
	int optimalBytesPerLine = 4;
	LineLayout optimalLayout = calculateHexLineLayout(optimalBytesPerLine);

	for (int candidate = 4; candidate <= 128; candidate += 4)
	{
		const auto layout = calculateHexLineLayout(candidate);
		if (layout.totalWidth >= viewportWidth)
			break;

		optimalBytesPerLine = candidate;
		optimalLayout = layout;
	}

	_bytesPerLine = optimalBytesPerLine;
	_hexStart = optimalLayout.hexStart;
	_asciiStart = optimalLayout.asciiStart;
}

CLightningFastViewerWidget::LineLayout CLightningFastViewerWidget::calculateHexLineLayout(int bytesPerLine) const
{
	const int hexWidth = (bytesPerLine * Layout::HEX_CHARS_PER_BYTE + Layout::HEX_MIDDLE_EXTRA_SPACE * ((bytesPerLine - 1) / Layout::HEX_BYTES_PER_GROUP)) * _charWidth;

	LineLayout layout;
	layout.hexStart = Layout::LEFT_MARGIN_PIXELS + (_nDigits + Layout::OFFSET_SUFFIX_CHARS) * _charWidth;
	layout.asciiStart = layout.hexStart + hexWidth + Layout::HEX_ASCII_SEPARATOR_CHARS * _charWidth;
	layout.totalWidth = layout.asciiStart + bytesPerLine * _charWidth;

	return layout;
}

void CLightningFastViewerWidget::drawHexLine(QPainter& painter, qsizetype offset, int y)
{
	if (offset >= _data.size())
		return;

	const int hScroll = horizontalScrollBar()->value();
	const int baseline = y + _fontMetrics.ascent();
	const QPalette& pal = palette();
	const ByteColors colors = byteColors(pal);
	const QColor selectedColor = pal.highlightedText().color();

	const qsizetype lineBytes = qMin(static_cast<qsizetype>(_bytesPerLine), _data.size() - offset);
	const char* const dataPtr = _data.constData(); // QByteArray::operator[] is the detaching overload here

	painter.setPen(pal.color(QPalette::Disabled, QPalette::Text));
	painter.drawText(Layout::LEFT_MARGIN_PIXELS - hScroll, baseline, QStringLiteral("%1:").arg(offset, _nDigits, 10, QChar('0')));
	painter.drawText(_asciiStart - _charWidth * Layout::HEX_ASCII_SEPARATOR_CHARS - hScroll, baseline, QStringLiteral("|"));

	// Both columns paint in runs of one colour, so a stretch of same-class bytes costs one drawText rather than one per byte.
	int runX = 0;
	QColor runColor;
	bool runSelected = false;

	_paintScratch.resize(0);

	const auto flushRun = [&] {
		if (_paintScratch.isEmpty())
			return;

		if (runSelected)
			painter.fillRect(runX - hScroll, y, static_cast<int>(_paintScratch.size()) * _charWidth, _lineHeight, pal.highlight());

		painter.setPen(runColor);
		painter.drawText(runX - hScroll, baseline, _paintScratch);
		_paintScratch.resize(0);
	};

	// columnX gives the x of the byte that opens a run; appendByte adds the byte's own characters, and any spacing that precedes them within a run.
	const auto paintColumn = [&](auto&& columnX, auto&& appendByte) {
		for (qsizetype i = 0; i < lineBytes; ++i)
		{
			const uint8_t byte = static_cast<uint8_t>(dataPtr[offset + i]);
			const bool selected = isSelected(offset + i);
			const QColor color = selected ? selectedColor : colors.forByte(byte);

			if (!_paintScratch.isEmpty() && (color != runColor || selected != runSelected))
				flushRun();

			if (_paintScratch.isEmpty())
			{
				runX = columnX(i);
				runColor = color;
				runSelected = selected;
			}

			appendByte(i, byte);
		}

		flushRun();
	};

	// The gap between groups goes into the run as a second space, so one run can span it
	paintColumn(
		[this](qsizetype i) { return _hexStart + static_cast<int>(i * Layout::HEX_CHARS_PER_BYTE + i / Layout::HEX_BYTES_PER_GROUP) * _charWidth; },
		[this](qsizetype i, uint8_t byte) {
			if (!_paintScratch.isEmpty())
			{
				if ((i % Layout::HEX_BYTES_PER_GROUP) == 0)
					_paintScratch += QChar(' ');
				_paintScratch += QChar(' ');
			}

			_paintScratch += QChar(hexChars[byte >> 4]);
			_paintScratch += QChar(hexChars[byte & 0x0F]);
		});

	paintColumn(
		[this](qsizetype i) { return _asciiStart + static_cast<int>(i) * _charWidth; },
		[this](qsizetype, uint8_t byte) { _paintScratch += _hexGlyphs[byte]; });
}

void CLightningFastViewerWidget::drawTextLine(QPainter& painter, qsizetype lineIndex, int y)
{
	const qsizetype lineStart = _lineOffsets[lineIndex];
	const qsizetype lineEnd = _lineOffsets[lineIndex + 1];
	const qsizetype textLength = _text.size();
	const QChar* const chars = _text.constData(); // QString::operator[] is the detaching overload here

	const int baseline = y + _fontMetrics.ascent();
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

		drawChars(chars + runStart, column - runColumn, runColumn, runSelected);
		runStart = -1;
	};

	const int viewportWidth = viewport()->width();

	for (qsizetype offset = lineStart; offset < lineEnd; ++offset)
	{
		if (originX + int(column) * _charWidth >= viewportWidth) // Columns only grow, so nothing past here is visible
			break;

		const QChar ch = chars[offset];
		const int columns = columnsForChar(ch, offset + 1 < textLength ? chars[offset + 1] : QChar(), column);
		if (columns == 0)
			continue;

		const bool selected = isSelected(offset);

		// Literal ASCII accumulates into a run: one drawText for the whole stretch instead of one per character
		if (isPrintableAscii(ch.unicode()))
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
				const QChar substitute = nonPrintableGlyph(ch);
				drawChars(&substitute, 1, column, selected);
			}
			else
				drawChars(chars + offset, ch.isHighSurrogate() && offset + 1 < textLength ? 2 : 1, column, selected);
		}

		column += columns;
	}

	flushRun();

	// A selected line terminator has no columns of its own, so a selection spanning lines would otherwise break at every line end
	if (chars[lineEnd - 1] == u'\n' && isSelected(lineEnd - 1))
		highlight(column, column + 1);
}

void CLightningFastViewerWidget::updateLayoutAndScrollBars()
{
	if (_mode == Mode::Hex)
		calculateHexLayout();

	verticalScrollBar()->setRange(0, static_cast<int>(qMax<qsizetype>(0, totalLines() - visibleLines())));
	verticalScrollBar()->setPageStep(visibleLines());
	verticalScrollBar()->setSingleStep(1);

	const qsizetype contentWidth = (_mode == Mode::Hex)
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
		return Region::Offset;
	if (x < _asciiStart - _charWidth * Layout::HEX_ASCII_SEPARATOR_CHARS)
		return Region::Hex;
	if (x >= _asciiStart)
		return Region::Ascii;
	return Region::None;
}

qsizetype CLightningFastViewerWidget::hexPosToOffset(const QPoint& pos, Region region) const
{
	if (_data.isEmpty())
		return -1;

	const int line = (pos.y() / _lineHeight) + verticalScrollBar()->value();
	if (line < 0 || line >= totalLines())
		return -1;

	const int x = pos.x() + horizontalScrollBar()->value();

	qsizetype lineOffset = line * _bytesPerLine;
	qsizetype byteInLine = 0;

	if (region == Region::Hex)
	{
		// Inverse of the column formula in drawHexLine
		// The gap following a group counts inside that group's span: a click on it clamps to the group's last byte
		const int cell = (x - _hexStart) / _charWidth;
		const int group = cell / Layout::HEX_GROUP_CHARS;
		const int byteInGroup = qMin((cell - group * Layout::HEX_GROUP_CHARS) / Layout::HEX_CHARS_PER_BYTE, Layout::HEX_BYTES_PER_GROUP - 1);
		byteInLine = qBound(0, group * Layout::HEX_BYTES_PER_GROUP + byteInGroup, _bytesPerLine - 1);
	}
	else if (region == Region::Ascii)
	{
		byteInLine = qBound(0, (x - _asciiStart) / _charWidth, _bytesPerLine - 1);
	}
	else
	{
		return -1;
	}

	qsizetype offset = lineOffset + byteInLine;
	return qMin(offset, (qsizetype)_data.size() - 1);
}

qsizetype CLightningFastViewerWidget::textPosToOffset(const QPoint& pos) const
{
	const qsizetype line = pos.y() / _lineHeight + verticalScrollBar()->value();
	if (line < 0 || line >= totalLines())
		return -1;

	return offsetAtColumn(line, qMax<qsizetype>(0, (pos.x() + horizontalScrollBar()->value() - Layout::LEFT_MARGIN_PIXELS) / _charWidth));
}

qsizetype CLightningFastViewerWidget::offsetAtColumn(qsizetype line, qsizetype targetColumn) const
{
	const qsizetype lineStart = _lineOffsets[line];
	const qsizetype lineEnd = _lineOffsets[line + 1];
	const qsizetype textLength = _text.size();
	const QChar* const chars = _text.constData();

	qsizetype column = 0;
	for (qsizetype offset = lineStart; offset < lineEnd; ++offset)
	{
		const int columns = columnsForChar(chars[offset], offset + 1 < textLength ? chars[offset + 1] : QChar(), column);
		if (columns == 0)
			continue;

		if (targetColumn < column + columns)
			return offset;

		column += columns;
	}

	return lineEnd - 1;
}

qsizetype CLightningFastViewerWidget::columnOfOffset(qsizetype line, qsizetype offset) const
{
	const qsizetype textLength = _text.size();
	const QChar* const chars = _text.constData();

	qsizetype column = 0;
	for (qsizetype i = _lineOffsets[line]; i < offset; ++i)
		column += columnsForChar(chars[i], i + 1 < textLength ? chars[i + 1] : QChar(), column);

	return column;
}

qsizetype CLightningFastViewerWidget::offsetLinesAway(qsizetype fromOffset, qsizetype lineDelta, qsizetype& column) const
{
	const qsizetype currentLine = findLineContainingOffset(fromOffset);
	if (currentLine < 0)
		return fromOffset;

	if (column < 0)
		column = columnOfOffset(currentLine, fromOffset);

	const qsizetype targetLine = qBound(qsizetype(0), currentLine + lineDelta, totalLines() - 1);
	return targetLine == currentLine ? fromOffset : offsetAtColumn(targetLine, column);
}

void CLightningFastViewerWidget::ensureVisible(qsizetype offset)
{
	qsizetype line = 0;

	if (_mode == Mode::Hex)
	{
		line = offset / _bytesPerLine;
	}
	else
	{
		line = findLineContainingOffset(offset);
		if (line < 0)
			return;
	}

	const int firstVisibleLine = verticalScrollBar()->value();
	if (line < firstVisibleLine)
		verticalScrollBar()->setValue(static_cast<int>(line));
	else if (line >= firstVisibleLine + visibleLines())
		verticalScrollBar()->setValue(static_cast<int>(line - visibleLines() + 1));

	if (_mode == Mode::Hex)
		return; // The hex layout is fitted to the viewport width, so there is nothing to scroll to horizontally

	const int x = Layout::LEFT_MARGIN_PIXELS + static_cast<int>(columnOfOffset(line, offset)) * _charWidth;
	const int firstVisibleX = horizontalScrollBar()->value();
	if (x < firstVisibleX)
		horizontalScrollBar()->setValue(x);
	else if (x + _charWidth > firstVisibleX + viewport()->width())
		horizontalScrollBar()->setValue(x + _charWidth - viewport()->width());
}

void CLightningFastViewerWidget::copySelection(Region format)
{
	if (!_selection.hasSelection())
		return;

	const qsizetype start = _selection.first();
	const qsizetype count = _selection.count();

	if (_mode == Mode::Text)
	{
		QApplication::clipboard()->setText(_text.mid(start, count));
		return;
	}

	const char* const bytes = _data.constData(); // QByteArray::operator[] is the detaching overload here

	if (format == Region::Hex)
	{
		QString hexStr;
		hexStr.reserve(count * Layout::HEX_CHARS_PER_BYTE);
		for (qsizetype i = 0; i < count; ++i)
		{
			const uint8_t byte = static_cast<uint8_t>(bytes[start + i]);
			if (i > 0)
				hexStr += ' ';
			hexStr += QChar(hexChars[byte >> 4]);
			hexStr += QChar(hexChars[byte & 0x0F]);
		}
		QApplication::clipboard()->setText(hexStr);
	}
	else
	{
		assert_r(format == Region::Ascii); // The menu offers only the two byte columns, and Ctrl+C passes the seeded region

		QString asciiStr;
		asciiStr.reserve(count);
		for (qsizetype i = 0; i < count; ++i)
		{
			const uint8_t byte = static_cast<uint8_t>(bytes[start + i]);
			asciiStr += isPrintableAscii(byte) ? QChar(byte) : QChar('.');
		}
		QApplication::clipboard()->setText(asciiStr);
	}
}

void CLightningFastViewerWidget::selectAll()
{
	const qsizetype size = (_mode == Mode::Hex) ? _data.size() : _text.size();
	if (size == 0)
		return;

	_selection.selectRange(0, size, (_mode == Mode::Hex) ? Region::Hex : Region::Ascii);
	viewport()->update();
}

bool CLightningFastViewerWidget::isSelected(qsizetype offset) const
{
	// The cursor cell paints as a block: there is no caret
	if (!_selection.hasSelection())
		return offset == _selection.cursor;

	return offset >= _selection.first() && offset <= _selection.last();
}

int CLightningFastViewerWidget::columnsForChar(QChar ch, QChar next, qsizetype column) const
{
	const char16_t code = ch.unicode();
	if (isPrintableAscii(code)) [[likely]]
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
	if (_mode != Mode::Text || !_geometrySettled)
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

	buildGlyphTables();
	_charColumns.clear();
	_wrappedForMaxColumns = -1; // Measured column counts, and the wrap width itself, follow the font
}

void CLightningFastViewerWidget::buildGlyphTables()
{
	// A glyph the font lacks arrives from a fallback font at its own advance. Text mode draws each stand-in on its own column origin, so anything that
	// fits the cell is safe there; the hex columns batch glyphs into one drawText, where each advances the next, so only an exact fit stays on the grid.
	const auto usable = [this](char16_t code, bool exactWidthRequired) {
		const QChar glyph(code);
		const bool inFont = _fontMetrics.inFont(glyph);
		const int advance = _fontMetrics.horizontalAdvance(glyph);
		const bool fits = exactWidthRequired ? advance == _charWidth : (advance > 0 && advance <= _charWidth);
		return inFont && fits ? glyph : QChar(fallbackGlyph);
	};

	_invisibleMarker = usable(invisibleMarkerGlyph, false);

	_nonPrintableGlyphs.fill(_invisibleMarker); // Covers the C1 controls, the soft hyphen, and every printable slot, which is never read
	for (char16_t code = 0; code < 0x20; ++code)
		_nonPrintableGlyphs[code] = usable(cp437ControlGlyphs[code], false);
	_nonPrintableGlyphs[0x7F] = usable(cp437DeleteGlyph, false);
	_nonPrintableGlyphs[0xA0] = usable(openBoxGlyph, false);

	for (char16_t byte = 0; byte < 256; ++byte)
	{
		if (isPrintableAscii(byte))
			_hexGlyphs[byte] = QChar(byte);
		else if (byte < 0x20)
			_hexGlyphs[byte] = usable(cp437ControlGlyphs[byte], true);
		else if (byte == 0x7F)
			_hexGlyphs[byte] = usable(cp437DeleteGlyph, true);
		else
			_hexGlyphs[byte] = usable(cp437HighGlyphs[byte - 0x80], true);
	}
}

QChar CLightningFastViewerWidget::nonPrintableGlyph(QChar ch) const
{
	const char16_t code = ch.unicode();
	return code < 0x100 ? _nonPrintableGlyphs[code] : _invisibleMarker;
}

qsizetype CLightningFastViewerWidget::findLineContainingOffset(qsizetype offset) const
{
	if (offset < 0 || offset >= _text.size())
		return -1;

	// The trailing sentinel guarantees a hit for any offset within the text
	const auto line = std::upper_bound(_lineOffsets.begin(), _lineOffsets.end(), offset);
	return line - _lineOffsets.begin() - 1;
}

void CLightningFastViewerWidget::moveToStart()
{
	moveCursorTo(0);
}

void CLightningFastViewerWidget::moveToEnd()
{
	const qsizetype size = (_mode == Mode::Hex) ? _data.size() : _text.size();
	moveCursorTo(qMax(qsizetype(0), size - 1));
}

void CLightningFastViewerWidget::moveCursorTo(qsizetype offset)
{
	_selection.placeCursor(offset);
	ensureVisible(offset);
	viewport()->update();
}

// Both ASCII and Latin-1 upper-case letters fold by the same bit, so this matches what QString case-insensitive comparison does over Latin-1
static constexpr char foldByte(char c)
{
	const uint8_t byte = static_cast<uint8_t>(c);
	const bool isUpper = (byte >= 'A' && byte <= 'Z') || (byte >= 0xC0 && byte <= 0xDE && byte != 0xD7);
	return isUpper ? static_cast<char>(byte + 0x20) : c;
}

// Folded copy of the same length, so a match offset in it is a match offset in the original
static QByteArray foldedBytes(const QByteArray& bytes)
{
	QByteArray folded;
	folded.resize(bytes.size());
	std::transform(bytes.cbegin(), bytes.cend(), folded.begin(), foldByte);

	return folded;
}

static bool isWordCharAt(const QString& haystack, qsizetype at)
{
	return haystack[at].isLetterOrNumber();
}

// Bytes are classified as ASCII: a hex view carries no encoding, so counting 0xC0 to 0xFF as letters would be a guess
static bool isWordCharAt(const QByteArray& haystack, qsizetype at)
{
	const uint8_t byte = static_cast<uint8_t>(haystack[at]);
	return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

// True when nothing word-like abuts the match on either side
template <typename Haystack>
static bool isWholeWordMatch(const Haystack& haystack, qsizetype pos, qsizetype len)
{
	return (pos == 0 || !isWordCharAt(haystack, pos - 1))
		&& (pos + len >= haystack.size() || !isWordCharAt(haystack, pos + len));
}

static qsizetype indexOfIn(const QString& haystack, const QString& needle, qsizetype from, bool backward, Qt::CaseSensitivity cs)
{
	return backward ? haystack.lastIndexOf(needle, from, cs) : haystack.indexOf(needle, from, cs);
}

// Only the exact bytes: QByteArray has no case-insensitive search, so a caller wanting one folds both sides first
static qsizetype indexOfIn(const QByteArray& haystack, const QByteArray& needle, qsizetype from, bool backward, Qt::CaseSensitivity cs)
{
	assert_r(cs == Qt::CaseSensitive);
	return backward ? haystack.lastIndexOf(needle, from) : haystack.indexOf(needle, from);
}

// Nearest match to 'from' in the given direction that 'accept' allows, stepping one position past each rejected match.
// A negative 'from' means nothing is left to search: lastIndexOf would read it as "start at the end".
// Returns {-1, 0} when no match is accepted.
template <typename Haystack, typename Needle, typename Accept>
static std::pair<qsizetype, qsizetype> acceptedLiteralMatch(
	const Haystack& haystack, const Needle& needle, qsizetype from, bool backward, Qt::CaseSensitivity cs, Accept accept)
{
	while (from >= 0)
	{
		const qsizetype matchPos = indexOfIn(haystack, needle, from, backward, cs);
		if (matchPos < 0)
			break;

		if (accept(matchPos, needle.size()))
			return { matchPos, needle.size() };

		from = backward ? matchPos - 1 : matchPos + 1;
	}

	return { -1, 0 };
}

// Accepted match nearest to 'from' in the given direction. QRegularExpression cannot search backwards, so both directions
// scan forward once, and 'accept' is applied inside that scan because a rejected match must not cost another one.
template <typename Accept>
static std::pair<qsizetype, qsizetype> acceptedRegexMatch(
	const QRegularExpression& rx, const QString& haystack, qsizetype from, bool backward, Accept accept)
{
	std::pair<qsizetype, qsizetype> found{ -1, 0 };

	for (auto it = rx.globalMatch(haystack, backward ? 0 : from); it.hasNext(); )
	{
		const QRegularExpressionMatch match = it.next();
		if (backward && match.capturedStart() > from)
			break;

		if (!accept(match.capturedStart(), match.capturedLength()))
			continue;

		found = { match.capturedStart(), match.capturedLength() };
		if (!backward)
			break;
	}

	return found;
}

qsizetype CLightningFastViewerWidget::searchStartOffset(bool backward, qsizetype haystackSize) const
{
	if (!_selection.hasCursor())
		return backward ? haystackSize - 1 : 0;

	if (!_selection.hasSelection())
		return _selection.cursor; // Nothing was matched yet, so the cursor's own position is still a candidate

	return backward ? _selection.first() - 1 : _selection.last() + 1;
}

const QString& CLightningFastViewerWidget::regexHaystack()
{
	if (_mode == Mode::Text)
		return _text;

	// Only a regex search needs the bytes as text, and it needs them all at once; literal search runs on _data itself
	if (_hexSearchText.isEmpty())
		_hexSearchText = QString::fromLatin1(_data);

	return _hexSearchText;
}

const QByteArray& CLightningFastViewerWidget::foldedData()
{
	if (_foldedData.isEmpty())
		_foldedData = foldedBytes(_data);

	return _foldedData;
}

bool CLightningFastViewerWidget::find(const QString& exp, QTextDocument::FindFlags options)
{
	if (exp.isEmpty())
		return false;

	const bool backward = options & QTextDocument::FindBackward;
	const bool wholeWords = options & QTextDocument::FindWholeWords;
	const Qt::CaseSensitivity cs = (options & QTextDocument::FindCaseSensitively) ? Qt::CaseSensitive : Qt::CaseInsensitive;

	const auto search = [&](const auto& haystack, const auto& needle, Qt::CaseSensitivity sensitivity) {
		const auto accept = [&](qsizetype pos, qsizetype len) { return !wholeWords || isWholeWordMatch(haystack, pos, len); };
		return acceptedLiteralMatch(haystack, needle, searchStartOffset(backward, haystack.size()), backward, sensitivity, accept);
	};

	// No byte can hold a character above U+00FF, and toLatin1 would fold one to '?' and match those bytes instead
	if (_mode == Mode::Hex && std::any_of(exp.cbegin(), exp.cend(), [](QChar ch) { return ch.unicode() > 0xFF; }))
		return false;

	// Hex mode searches the bytes themselves: converting the needle is free, converting the file would cost two bytes per byte on every call
	// Folding both sides puts a case-insensitive search on QByteArray's exact search
	// Folding is per byte, so offsets and the ASCII word classification both survive it
	const auto searchBytes = [&] {
		return cs == Qt::CaseSensitive
			? search(_data, exp.toLatin1(), Qt::CaseSensitive)
			: search(foldedData(), foldedBytes(exp.toLatin1()), Qt::CaseSensitive);
	};

	const auto [matchPos, matchLen] = (_mode == Mode::Text) ? search(_text, exp, cs) : searchBytes();
	if (matchPos < 0)
		return false;

	_selection.selectRange(matchPos, matchLen, Region::Ascii);
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

	QRegularExpression rx = exp;
	if (options & QTextDocument::FindCaseSensitively)
		rx.setPatternOptions(rx.patternOptions() & ~QRegularExpression::CaseInsensitiveOption);
	else
		rx.setPatternOptions(rx.patternOptions() | QRegularExpression::CaseInsensitiveOption);

	const QString& haystack = regexHaystack();

	// An empty match has nothing to show and nothing to step over, so a pattern that allows one would return the same position forever
	const auto accept = [&](qsizetype pos, qsizetype len) {
		return len > 0 && (!wholeWords || isWholeWordMatch(haystack, pos, len));
	};

	const auto [matchPos, matchLen] = acceptedRegexMatch(rx, haystack, searchStartOffset(backward, haystack.size()), backward, accept);
	if (matchPos < 0)
		return false;

	_selection.selectRange(matchPos, matchLen, Region::Ascii);
	ensureVisible(matchPos);
	viewport()->update();
	return true;
}

qsizetype CLightningFastViewerWidget::selectionStart() const
{
	return _selection.hasSelection() ? _selection.first() : -1;
}

void CLightningFastViewerWidget::updateCursorShape(const QPoint& pos)
{
	bool overText = false;

	if (_mode == Mode::Hex)
	{
		Region region = regionAtPos(pos);
		overText = (region == Region::Hex || region == Region::Ascii);
	}
	else
	{
		qsizetype offset = textPosToOffset(pos);
		overText = (offset >= 0);
	}

	viewport()->setCursor(overText ? Qt::IBeamCursor : Qt::ArrowCursor);
}
