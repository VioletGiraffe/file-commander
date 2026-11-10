#include "cterminalscreen.h"

DISABLE_COMPILER_WARNINGS
#include <QStringTokenizer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

CTerminalScreen::CTerminalScreen(int rows, int columns) :
	_lines(static_cast<size_t>(rows), QString(columns, u' ')),
	_columns(columns)
{
}

void CTerminalScreen::feed(QStringView input)
{
	QString text = std::exchange(_incompleteSequence, {});
	text += input;
	for (qsizetype i = 0; i < text.size();)
	{
		const QChar c = text[i];
		if (c == u'\x1b')
		{
			const qsizetype end = escape(text, i);
			if (end < 0)
			{
				_incompleteSequence = text.sliced(i);
				return;
			}
			i = end;
			continue;
		}

		switch (c.unicode())
		{
		case u'\r':
			moveCursorTo(_row, 0);
			break;
		case u'\n':
		case u'\v':
		case u'\f':
			lineFeed();
			break;
		case u'\b':
			moveCursorTo(_row, _column - 1);
			break;
		case u'\t':
			moveCursorTo(_row, (_column / 8 + 1) * 8);
			break;
		default:
			if (c.unicode() >= 0x20 && c.unicode() != 0x7F)
				put(c);
		}
		++i;
	}
}

QString CTerminalScreen::text() const
{
	QString text;
	for (int index = 0; index < rows(); ++index)
		text += QStringLiteral("%1|").arg(index, 2) + _lines[(size_t)index] + u'\n';
	return text;
}

bool CTerminalScreen::contains(QStringView text) const
{
	return std::any_of(_lines.cbegin(), _lines.cend(), [text](const QString& line) { return line.contains(text); });
}

QByteArray CTerminalScreen::takeReplies()
{
	return std::exchange(_replies, {});
}

qsizetype CTerminalScreen::escape(QStringView text, qsizetype start)
{
	if (start + 1 >= text.size())
		return -1;

	const QChar kind = text[start + 1];
	if (kind == u'[')
	{
		qsizetype end = start + 2;
		while (end < text.size() && (text[end] < u'@' || text[end] > u'~'))
			++end;
		if (end >= text.size())
			return -1;

		csi(text.sliced(start + 2, end - start - 2), text[end]);
		return end + 1;
	}

	// OSC, DCS, APC and PM strings end with BEL or ST
	if (kind == u']' || kind == u'P' || kind == u'_' || kind == u'^')
	{
		for (qsizetype end = start + 2; end < text.size(); ++end)
		{
			if (text[end] == u'\a')
				return end + 1;
			if (text[end] == u'\x1b')
			{
				if (end + 1 >= text.size())
					return -1;
				return text[end + 1] == u'\\' ? end + 2 : end;
			}
		}
		return -1;
	}

	// Character set designations take one more character
	if (kind == u'(' || kind == u')' || kind == u'*' || kind == u'+')
		return start + 2 < text.size() ? start + 3 : -1;

	switch (kind.unicode())
	{
	case u'7':
		_savedRow = _row;
		_savedColumn = _column;
		break;
	case u'8':
		moveCursorTo(_savedRow, _savedColumn);
		break;
	case u'D':
		lineFeed();
		break;
	case u'E':
		moveCursorTo(_row, 0);
		lineFeed();
		break;
	case u'M':
		reverseLineFeed();
		break;
	default:
		break;
	}
	return start + 2;
}

void CTerminalScreen::csi(QStringView parameters, QChar final)
{
	if (!parameters.isEmpty() && (parameters[0] == u'?' || parameters[0] == u'>' || parameters[0] == u'<' || parameters[0] == u'='))
		return; // Private modes and queries

	std::vector<int> values;
	for (QStringView part : qTokenize(parameters, u';'))
		values.push_back(part.toInt()); // An empty or malformed part is 0, the default
	const auto value = [&values](size_t index) { return index < values.size() ? values[index] : 0; };
	const int first = value(0);
	const int count = std::max(first, 1);
	const QString blankLine(_columns, u' ');

	switch (final.unicode())
	{
	case u'H':
	case u'f':
		moveCursorTo(std::max(first, 1) - 1, std::max(value(1), 1) - 1);
		break;
	case u'G':
	case u'`':
		moveCursorTo(_row, count - 1);
		break;
	case u'd':
		moveCursorTo(count - 1, _column);
		break;
	case u'A':
		moveCursorTo(_row - count, _column);
		break;
	case u'B':
	case u'e':
		moveCursorTo(_row + count, _column);
		break;
	case u'C':
	case u'a':
		moveCursorTo(_row, _column + count);
		break;
	case u'D':
		moveCursorTo(_row, _column - count);
		break;
	case u'E':
		moveCursorTo(_row + count, 0);
		break;
	case u'F':
		moveCursorTo(_row - count, 0);
		break;
	case u'J':
		for (int row = 0; row < rows(); ++row)
		{
			if ((first == 0 && row > _row) || (first == 1 && row < _row) || first >= 2)
				clear(row, 0, _columns);
		}
		if (first == 0)
			clear(_row, _column, _columns);
		else if (first == 1)
			clear(_row, 0, _column + 1);
		break;
	case u'K':
		if (first == 0)
			clear(_row, _column, _columns);
		else if (first == 1)
			clear(_row, 0, _column + 1);
		else
			clear(_row, 0, _columns);
		break;
	case u'X':
		clear(_row, _column, std::min(_column + count, _columns));
		break;
	case u'@':
	{
		QString& line = _lines[(size_t)_row];
		line.insert(_column, QString(std::min(count, _columns - _column), u' '));
		line.truncate(_columns);
		break;
	}
	case u'P':
	{
		QString& line = _lines[(size_t)_row];
		line.remove(_column, std::min(count, _columns - _column));
		line += QString(_columns - line.size(), u' ');
		break;
	}
	case u'L':
	case u'M':
	{
		const int lineCount = std::min(count, rows() - _row);
		const auto cursorLine = _lines.begin() + _row;
		if (final == u'L')
		{
			_lines.insert(cursorLine, (size_t)lineCount, blankLine);
			_lines.resize(_lines.size() - (size_t)lineCount);
		}
		else
		{
			_lines.erase(cursorLine, cursorLine + lineCount);
			_lines.insert(_lines.end(), (size_t)lineCount, blankLine);
		}
		break;
	}
	case u'S':
		for (int i = 0; i < std::min(count, rows()); ++i)
		{
			_lines.erase(_lines.begin());
			_lines.push_back(blankLine);
		}
		break;
	case u'T':
		for (int i = 0; i < std::min(count, rows()); ++i)
		{
			_lines.pop_back();
			_lines.insert(_lines.begin(), blankLine);
		}
		break;
	case u'n':
		if (first == 6)
			_replies += QStringLiteral("\x1b[%1;%2R").arg(_row + 1).arg(_column + 1).toLatin1();
		break;
	case u's':
		_savedRow = _row;
		_savedColumn = _column;
		break;
	case u'u':
		moveCursorTo(_savedRow, _savedColumn);
		break;
	default:
		break; // SGR and the rest change nothing shown as text
	}
}

void CTerminalScreen::put(QChar c)
{
	if (_wrapPending)
	{
		moveCursorTo(_row, 0);
		lineFeed();
	}

	_lines[(size_t)_row][_column] = c;
	if (_column + 1 < _columns)
		++_column;
	else
		_wrapPending = true;
}

void CTerminalScreen::lineFeed()
{
	_wrapPending = false;
	if (_row + 1 < rows())
	{
		++_row;
		return;
	}

	_lines.erase(_lines.begin());
	_lines.emplace_back(_columns, u' ');
}

void CTerminalScreen::reverseLineFeed()
{
	_wrapPending = false;
	if (_row > 0)
	{
		--_row;
		return;
	}

	_lines.pop_back();
	_lines.insert(_lines.begin(), QString(_columns, u' '));
}

void CTerminalScreen::clear(int row, int fromColumn, int toColumn)
{
	QString& line = _lines[(size_t)row];
	for (int column = std::max(fromColumn, 0); column < std::min(toColumn, _columns); ++column)
		line[column] = u' ';
}

void CTerminalScreen::moveCursorTo(int row, int column)
{
	_row = std::clamp(row, 0, rows() - 1);
	_column = std::clamp(column, 0, _columns - 1);
	_wrapPending = false;
}
