#pragma once

#include <QAbstractScrollArea>
#include <QPainter>
#include <QFontMetrics>
#include <QScrollBar>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>
#include <QtMath>

class LightningFastViewer final : public QAbstractScrollArea
{
public:
	explicit LightningFastViewer(QWidget* parent = nullptr)
		: QAbstractScrollArea(parent)
	{
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
		setFocusPolicy(Qt::StrongFocus);
		viewport()->setCursor(Qt::IBeamCursor);
	}

	void setData(const QByteArray& data)
	{
		_data = data;
		_selection = Selection();
		updateScrollBars();
		viewport()->update();
	}

	const QByteArray& data() const
	{
		return _data;
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter painter(viewport());
		painter.setFont(font());

		QFontMetrics fm(font());
		_lineHeight = fm.height();
		_charWidth = fm.horizontalAdvance('0');

		// Calculate layout positions
		calculateLayout();

		// Calculate which lines are visible
		const qsizetype firstLine = verticalScrollBar()->value();
		const qsizetype visibleLines = viewport()->height() / _lineHeight + 2;
		const qsizetype lastLine = qMin(firstLine + visibleLines, totalLines());

		// Draw lines
		int y = 0;
		for (qsizetype line = firstLine; line < lastLine; ++line)
		{
			const qsizetype offset = line * _bytesPerLine;
			drawLine(painter, offset, y, fm);
			y += _lineHeight;
		}
	}

	void resizeEvent(QResizeEvent* event) override
	{
		QAbstractScrollArea::resizeEvent(event);
		updateScrollBars();
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton)
		{
			qsizetype offset = posToOffset(event->pos());
			if (offset >= 0)
			{
				_selection.start = _selection.end = offset;
				_selection.region = regionAtPos(event->pos());
				viewport()->update();
			}
		}
	}

	void mouseMoveEvent(QMouseEvent* event) override
	{
		if (event->buttons() & Qt::LeftButton && _selection.start >= 0)
		{
			qsizetype offset = posToOffset(event->pos());
			if (offset >= 0)
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

	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton)
		{
			// Select the entire byte
			qsizetype offset = posToOffset(event->pos());
			if (offset >= 0 && offset < _data.size())
			{
				_selection.start = offset;
				_selection.end = offset;
				_selection.region = regionAtPos(event->pos());
				viewport()->update();
			}
		}
	}

	void keyPressEvent(QKeyEvent* event) override
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
		qsizetype cursorPos = _selection.end >= 0 ? _selection.end : 0;
		qsizetype newPos = cursorPos;

		switch (event->key())
		{
		case Qt::Key_Left:
			newPos = qMax(0LL, cursorPos - 1);
			break;
		case Qt::Key_Right:
			newPos = qMin((qsizetype)_data.size() - 1, cursorPos + 1);
			break;
		case Qt::Key_Up:
			newPos = qMax(0LL, cursorPos - _bytesPerLine);
			break;
		case Qt::Key_Down:
			newPos = qMin((qsizetype)_data.size() - 1, cursorPos + _bytesPerLine);
			break;
		case Qt::Key_PageUp:
			newPos = qMax(0LL, cursorPos - _bytesPerLine * (viewport()->height() / _lineHeight));
			break;
		case Qt::Key_PageDown:
			newPos = qMin((qsizetype)_data.size() - 1,
						  cursorPos + _bytesPerLine * (viewport()->height() / _lineHeight));
			break;
		case Qt::Key_Home:
			newPos = ctrlPressed ? 0 : (cursorPos / _bytesPerLine) * _bytesPerLine;
			break;
		case Qt::Key_End:
			if (ctrlPressed)
			{
				newPos = _data.size() - 1;
			}
			else
			{
				qsizetype lineStart = (cursorPos / _bytesPerLine) * _bytesPerLine;
				newPos = qMin((qsizetype)_data.size() - 1, lineStart + _bytesPerLine - 1);
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

	void wheelEvent(QWheelEvent* event) override
	{
		QAbstractScrollArea::wheelEvent(event);
	}

private:
	enum Region { REGION_OFFSET, REGION_HEX, REGION_ASCII, REGION_NONE };

	struct Selection
	{
		qsizetype start = -1;
		qsizetype end = -1;
		Region region = REGION_NONE;

		bool isValid() const { return start >= 0 && end >= 0; }

		qsizetype selStart() const { return qMin(start, end); }
		qsizetype selEnd() const { return qMax(start, end); }
	};

	int totalLines() const
	{
		if (_data.isEmpty())
			return 0;

		return static_cast<int>((_data.size() + (qsizetype)_bytesPerLine - 1) / (qsizetype)_bytesPerLine);
	}

	void calculateLayout()
	{
		QFontMetrics fm(font());

		// Offset area: "00000000: "
		int nDigits = static_cast<int>(qCeil(::log10(static_cast<double>(_data.size() + 1))));
		nDigits = qMax(4, nDigits); // Minimum 8 digits
		_offsetWidth = fm.horizontalAdvance(QString(nDigits, '0') + ": ");

		// Hex area starts after offset
		_hexStart = _offsetWidth;

		// ASCII area starts after hex (16 bytes * 3 chars + extra space at middle)
		const int hexWidth = _charWidth * (_bytesPerLine * 3 + 1);
		_asciiStart = _hexStart + hexWidth + _charWidth * 3; // " | "
	}

	void drawLine(QPainter& painter, qsizetype offset, int y, const QFontMetrics& fm)
	{
		if (offset >= _data.size()) return;

		QString line;
		line.reserve(100);

		// Draw offset
		int nDigits = static_cast<int>(qCeil(::log10(static_cast<double>(_data.size() + 1))));
		nDigits = qMax(4, nDigits);
		QString offsetStr = QString("%1").arg(offset, nDigits, 10, QChar('0'));

		painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
		painter.drawText(_charWidth, y + fm.ascent(), offsetStr + ":");

		painter.setPen(palette().color(QPalette::Text));

		const qsizetype lineBytes = qMin(static_cast<qsizetype>(_bytesPerLine), _data.size() - offset);

		// Draw hex bytes
		int x = _hexStart;
		for (int i = 0; i < _bytesPerLine; ++i)
		{
			if (i < lineBytes)
			{
				const unsigned char byte = static_cast<unsigned char>(_data[offset + i]);
				qsizetype byteOffset = offset + i;

				// Check if this byte is selected
				if (_selection.isValid() &&
					byteOffset >= _selection.selStart() &&
					byteOffset <= _selection.selEnd())
				{
					QRect selRect(x, y, _charWidth * 2, _lineHeight);
					painter.fillRect(selRect, palette().highlight());
					painter.setPen(palette().highlightedText().color());
				}
				else
				{
					painter.setPen(palette().color(QPalette::Text));
				}

				QString hexByte;
				hexByte += QChar(hexChars[byte >> 4]);
				hexByte += QChar(hexChars[byte & 0x0F]);

				painter.drawText(x, y + fm.ascent(), hexByte);
				x += _charWidth * 3; // 2 chars + space

				if (i == 7)
				{
					x += _charWidth; // Extra space in middle
				}
			}
		}

		// Draw separator
		painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
		painter.drawText(_asciiStart - _charWidth * 3, y + fm.ascent(), " | ");

		// Draw ASCII
		x = _asciiStart;
		for (qsizetype i = 0; i < lineBytes; ++i)
		{
			const unsigned char byte = static_cast<unsigned char>(_data[offset + i]);
			qsizetype byteOffset = offset + i;

			// Check if this byte is selected
			if (_selection.isValid() &&
				byteOffset >= _selection.selStart() &&
				byteOffset <= _selection.selEnd())
			{
				QRect selRect(x, y, _charWidth, _lineHeight);
				painter.fillRect(selRect, palette().highlight());
				painter.setPen(palette().highlightedText().color());
			}
			else
			{
				painter.setPen(palette().color(QPalette::Text));
			}

			QChar ch = (byte >= 32 && byte <= 126) ? QChar(byte) : QChar('.');
			painter.drawText(x, y + fm.ascent(), ch);
			x += _charWidth;
		}
	}

	void updateScrollBars()
	{
		if (_lineHeight == 0)
		{
			QFontMetrics fm(font());
			_lineHeight = fm.height();
			_charWidth = fm.horizontalAdvance('0');
		}

		calculateLayout();

		const int visibleLines = viewport()->height() / _lineHeight;
		verticalScrollBar()->setRange(0, qMax(0, totalLines() - visibleLines));
		verticalScrollBar()->setPageStep(visibleLines);
		verticalScrollBar()->setSingleStep(1);

		// Horizontal scrollbar
		int totalWidth = _asciiStart + _charWidth * _bytesPerLine + _charWidth * 2;
		horizontalScrollBar()->setRange(0, qMax(0, totalWidth - viewport()->width()));
		horizontalScrollBar()->setPageStep(viewport()->width());
	}

	Region regionAtPos(const QPoint& pos) const
	{
		int x = pos.x() + horizontalScrollBar()->value();

		if (x < _hexStart) return REGION_OFFSET;
		if (x < _asciiStart - _charWidth * 3) return REGION_HEX;
		if (x >= _asciiStart) return REGION_ASCII;
		return REGION_NONE;
	}

	qsizetype posToOffset(const QPoint& pos) const
	{
		if (_data.isEmpty()) return -1;

		int line = (pos.y() / _lineHeight) + verticalScrollBar()->value();
		if (line < 0 || line >= totalLines()) return -1;

		int x = pos.x() + horizontalScrollBar()->value();
		Region region = regionAtPos(pos);

		qsizetype lineOffset = line * _bytesPerLine;
		int byteInLine = 0;

		if (region == REGION_HEX)
		{
			int relX = x - _hexStart;
			byteInLine = relX / (_charWidth * 3);

			// Account for extra space at position 8
			if (byteInLine >= 8)
			{
				relX -= _charWidth;
				byteInLine = relX / (_charWidth * 3);
			}

			byteInLine = qBound(0, byteInLine, _bytesPerLine - 1);
		}
		else if (region == REGION_ASCII)
		{
			int relX = x - _asciiStart;
			byteInLine = relX / _charWidth;
			byteInLine = qBound(0, byteInLine, _bytesPerLine - 1);
		}
		else
		{
			return -1;
		}

		qsizetype offset = lineOffset + byteInLine;
		return qMin(offset, (qsizetype)_data.size() - 1);
	}

	void ensureVisible(qsizetype offset)
	{
		int line = static_cast<int>(offset / _bytesPerLine);
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

	void copySelection()
	{
		if (!_selection.isValid()) return;

		qsizetype start = _selection.selStart();
		qsizetype end = _selection.selEnd();

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

	void selectAll()
	{
		if (!_data.isEmpty())
		{
			_selection.start = 0;
			_selection.end = _data.size() - 1;
			_selection.region = REGION_HEX;
			viewport()->update();
		}
	}

private:
	QByteArray _data;
	int _bytesPerLine = 16;
	int _lineHeight = 0;
	int _charWidth = 0;
	Selection _selection;

	// Layout positions (calculated in calculateLayout)
	int _offsetWidth = 0;
	int _hexStart = 0;
	int _asciiStart = 0;

	static constexpr char hexChars[] = "0123456789ABCDEF";
};
