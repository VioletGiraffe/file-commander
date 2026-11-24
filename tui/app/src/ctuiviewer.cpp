#include "ctuiviewer.h"
#include "ctuidialogs.h"
#include "ctuifields.h"
#include "ctuipalette.h"
#include "ctuipanel.h"

#include "filesystemhelperfunctions.h"
#include "settings.h"

#include "assert/advanced_assert.h"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TSItem
#define Uses_TText
#define Uses_TView
#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStringDecoder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <climits>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <utility>
#include <vector>

// What to find, as the find dialog sets it
struct SearchSpec
{
	QString text;
	bool caseSensitive = false;
	bool wholeWords = false;
	bool regex = false;
};

namespace {

// As in the GUI's text viewer: a larger file shows as Latin-1, without detection
constexpr qsizetype maxSizeForEncodingDetection = 25'000'000;
constexpr int tabWidth = 4;
constexpr int wheelStep = 3;
constexpr int findTextLimit = 4096; // In bytes

// The check boxes' items, in their order
enum FindOption { CaseSensitive, WholeWords, Regex };

struct DecodedText
{
	QString text;
	QString encodingName;
};

// Empty when the bytes are not valid in the encoding
[[nodiscard]] std::optional<QString> strictlyDecoded(const QByteArray& data, const QStringConverter::Encoding encoding)
{
	QStringDecoder decoder{ encoding };
	QString text = decoder.decode(data);
	if (decoder.hasError())
		return std::nullopt;
	return text;
}

[[nodiscard]] DecodedText decodedAs(const QByteArray& data, const QStringConverter::Encoding encoding, QString encodingName)
{
	return { QStringDecoder{ encoding }.decode(data), std::move(encodingName) };
}

// Empty when the file is not text in this encoding. Never called for hex.
[[nodiscard]] std::optional<DecodedText> decoded(const QByteArray& data, const ViewAs viewAs)
{
	switch (viewAs)
	{
	case ViewAs::DetectedEncoding:
		if (data.size() > maxSizeForEncodingDetection)
			return decoded(data, ViewAs::Latin1);
		if (const std::optional<QStringConverter::Encoding> bomEncoding = QStringConverter::encodingForData(data))
			return decodedAs(data, *bomEncoding, QString::fromLatin1(QStringConverter::nameForEncoding(*bomEncoding)));
		if (data.contains('\0')) // Binary content, as in the GUI's text viewer
			return decoded(data, ViewAs::Latin1);
		if (std::optional<DecodedText> utf8 = decoded(data, ViewAs::Utf8))
			return utf8;
		return decoded(data, ViewAs::SystemLocale);
	case ViewAs::Latin1:
		return DecodedText{ QString::fromLatin1(data), QStringLiteral("Latin-1") };
	case ViewAs::SystemLocale:
		return decodedAs(data, QStringConverter::System, QStringLiteral("System locale"));
	case ViewAs::Utf8:
	{
		// A NUL byte is valid UTF-8, but marks binary content
		if (data.contains('\0'))
			return std::nullopt;
		if (const std::optional<QStringConverter::Encoding> bomEncoding = QStringConverter::encodingForData(data); bomEncoding && *bomEncoding != QStringConverter::Utf8)
			return std::nullopt;
		std::optional<QString> text = strictlyDecoded(data, QStringConverter::Utf8);
		if (!text)
			return std::nullopt;
		return DecodedText{ std::move(*text), QStringLiteral("UTF-8") };
	}
	case ViewAs::Utf16:
		return decodedAs(data, QStringConverter::Utf16, QStringLiteral("UTF-16")); // The BOM's byte order, else the host's
	case ViewAs::Hex:
		break;
	}
	assert_and_return_unconditional_r("Hex is not decoded", std::nullopt);
}

// As Turbo Vision draws it: a control character as one code page glyph
[[nodiscard]] int cellsOf(const char32_t codePoint)
{
	const auto measured = [codePoint] { return (int)TText::width(stringView(QString::fromUcs4(&codePoint, 1).toUtf8())); };
	if (codePoint > 0xFFFF)
		return measured();

	static std::vector<uint8_t> bmpCells(0x10000, uint8_t{ 0xFF }); // 0xFF: not measured yet
	uint8_t& cells = bmpCells[codePoint];
	if (cells == 0xFF)
		cells = (uint8_t)measured();
	return cells;
}

struct CharLayout
{
	qsizetype length; // In UTF-16 units
	int cells;
};

// The character at `index`, its cells counted from `column`. A line break takes no cells, CR-LF's CR included.
[[nodiscard]] CharLayout charLayout(const QChar* chars, const qsizetype size, const qsizetype index, const qsizetype column)
{
	const char16_t code = chars[index].unicode();
	if (code >= 0x20 && code < 0x7F) [[likely]]
		return { 1, 1 };
	if (code == u'\n' || (code == u'\r' && index + 1 < size && chars[index + 1] == u'\n'))
		return { 1, 0 };
	if (code == u'\t')
		return { 1, (int)(tabWidth - column % tabWidth) };
	if (QChar::isHighSurrogate(code) && index + 1 < size && chars[index + 1].isLowSurrogate())
		return { 2, cellsOf(QChar::surrogateToUcs4(code, chars[index + 1].unicode())) };
	return { 1, cellsOf(code) };
}

[[nodiscard]] bool isLineBreak(const QChar ch, const CharLayout& layout)
{
	return layout.cells == 0 && (ch == u'\n' || ch == u'\r');
}

// The hex row: the offset, the bytes in groups of eight, a bar, then the bytes as ASCII
constexpr int hexBytesPerGroup = 8;

[[nodiscard]] int hexOffsetDigits(const qsizetype dataSize)
{
	return std::max((int)QByteArray::number(std::max<qsizetype>(dataSize - 1, 0), 16).size(), 8);
}

[[nodiscard]] int hexByteColumn(const int offsetDigits, const int index)
{
	return offsetDigits + 2 + index * 3 + index / hexBytesPerGroup;
}

// A space after the last byte
[[nodiscard]] int hexBarColumn(const int offsetDigits, const int bytesPerRow)
{
	return hexByteColumn(offsetDigits, bytesPerRow - 1) + 3;
}

[[nodiscard]] int hexAsciiColumn(const int offsetDigits, const int bytesPerRow)
{
	return hexBarColumn(offsetDigits, bytesPerRow) + 2;
}

// As many as fit, in steps of 4 as in the GUI's viewer, and at least 4
[[nodiscard]] int hexBytesPerRow(const int width, const int offsetDigits)
{
	int bytesPerRow = 4;
	for (int candidate = 8; candidate <= 128 && hexAsciiColumn(offsetDigits, candidate) + candidate <= width; candidate += 4)
		bytesPerRow = candidate;
	return bytesPerRow;
}

// Matches nothing unless the spec is a regex
[[nodiscard]] QRegularExpression regexFor(const SearchSpec& spec)
{
	return QRegularExpression{ spec.regex ? spec.text : QString{}, spec.caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption };
}

struct Match
{
	qsizetype start;
	qsizetype length;

	[[nodiscard]] qsizetype end() const noexcept { return start + length; }
};

// asciiWords: only ASCII letters and digits make words, as in the GUI's hex view, which carries no encoding
[[nodiscard]] bool isWordChar(const QChar ch, const bool asciiWords)
{
	return (!asciiWords || ch.unicode() < 0x80) && ch.isLetterOrNumber();
}

// An empty match has nothing to show, and a search stepping past it would find it again
[[nodiscard]] bool isAcceptable(const QString& haystack, const Match& match, const SearchSpec& spec, const bool asciiWords)
{
	if (match.length == 0)
		return false;
	return !spec.wholeWords || ((match.start == 0 || !isWordChar(haystack[match.start - 1], asciiWords)) && (match.end() >= haystack.size() || !isWordChar(haystack[match.end()], asciiWords)));
}

// The first acceptable match starting at `from` or after it
[[nodiscard]] std::optional<Match> firstMatchFrom(const QString& haystack, const SearchSpec& spec, const QRegularExpression& regex, qsizetype from, const bool asciiWords)
{
	const Qt::CaseSensitivity caseSensitivity = spec.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
	while (from <= haystack.size())
	{
		Match match{ 0, 0 };
		if (spec.regex)
		{
			const QRegularExpressionMatch found = regex.match(haystack, from);
			if (!found.hasMatch())
				return std::nullopt;
			match = { found.capturedStart(), found.capturedLength() };
		}
		else
		{
			const qsizetype start = haystack.indexOf(spec.text, from, caseSensitivity);
			if (start < 0)
				return std::nullopt;
			match = { start, spec.text.size() };
		}

		if (isAcceptable(haystack, match, spec, asciiWords))
			return match;
		from = match.start + 1;
	}
	return std::nullopt;
}

// The last acceptable match starting before `before`. A regex is matched forward from the start: it cannot run backward.
[[nodiscard]] std::optional<Match> lastMatchBefore(const QString& haystack, const SearchSpec& spec, const QRegularExpression& regex, const qsizetype before, const bool asciiWords)
{
	if (spec.regex)
	{
		std::optional<Match> last;
		for (std::optional<Match> match = firstMatchFrom(haystack, spec, regex, 0, asciiWords); match && match->start < before; match = firstMatchFrom(haystack, spec, regex, match->start + 1, asciiWords))
			last = match;
		return last;
	}

	const Qt::CaseSensitivity caseSensitivity = spec.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
	// lastIndexOf() takes a negative start as counted from the end
	for (qsizetype from = before - 1; from >= 0;)
	{
		const qsizetype start = haystack.lastIndexOf(spec.text, from, caseSensitivity);
		if (start < 0)
			return std::nullopt;

		const Match match{ start, spec.text.size() };
		if (isAcceptable(haystack, match, spec, asciiWords))
			return match;
		from = start - 1;
	}
	return std::nullopt;
}

enum class FindOutcome { Found, FoundAfterWrapAround, NotFound };

// The find options last saved, shared with the GUI's find bar; no text
[[nodiscard]] SearchSpec savedSearchOptions()
{
	const QSettings settings;
	return {
		.caseSensitive = settings.value(KEY_TEXT_VIEWER_FIND_CASE_SENSITIVE, false).toBool(),
		.wholeWords = settings.value(KEY_TEXT_VIEWER_FIND_WHOLE_WORDS, false).toBool(),
		.regex = settings.value(KEY_TEXT_VIEWER_FIND_REGEX, false).toBool()
	};
}

// Starts with the current text, else the history's most recent. Empty when cancelled, or confirmed without text.
// Saves the text and the options, which the GUI's find bar shares.
[[nodiscard]] std::optional<SearchSpec> askForSearch(const SearchSpec& current)
{
	CTuiSavedFieldHistory history{ KEY_TEXT_VIEWER_FIND_HISTORY, viewerFindHistoryId };

	auto* dialog = new TDialog({ 0, 0, 60, 9 }, "Find");
	dialog->options |= ofCentered;
	TInputLine* const input = insertFieldWithHistory(*dialog, history, "~T~ext:", 2, 2, 10, 58, findTextLimit).input;
	auto* const options = new TCheckBoxes({ 10, 4, 58, 5 }, new TSItem("Match ~c~ase", new TSItem("~W~hole words", new TSItem("Re~g~ex", nullptr))));
	dialog->insert(options);
	dialog->insert(new CTuiButton({ 17, 6, 29, 8 }, "Find", cmOK, bfDefault));
	dialog->insert(new CTuiButton({ 31, 6, 43, 8 }, "Cancel", cmCancel, bfNormal));

	setInputText(input, current.text.isEmpty() ? history.mostRecent() : current.text);
	setChecked(options, { current.caseSensitive, current.wholeWords, current.regex });
	input->select();

	const ushort command = TProgram::application->execView(dialog); // In front of the viewer, which is in front of the desktop
	const SearchSpec search{
		.text = inputText(input),
		.caseSensitive = isChecked(options, CaseSensitive),
		.wholeWords = isChecked(options, WholeWords),
		.regex = isChecked(options, Regex)
	};
	TObject::destroy(dialog);

	if (command != cmOK || search.text.isEmpty())
		return std::nullopt;

	history.record(search.text);
	QSettings settings;
	settings.setValue(KEY_TEXT_VIEWER_FIND_CASE_SENSITIVE, search.caseSensitive);
	settings.setValue(KEY_TEXT_VIEWER_FIND_WHOLE_WORDS, search.wholeWords);
	settings.setValue(KEY_TEXT_VIEWER_FIND_REGEX, search.regex);
	return search;
}

// The viewer's keys: the GUI text viewer's, and Esc
[[nodiscard]] std::optional<ushort> commandForKey(const KeyDownEvent& key)
{
	switch (key.keyCode)
	{
	case kbEsc: return cmCancel;
	case kbCtrlF: return cmFindInViewer;
	case kbF3: return cmFindNextInViewer;
	case kbShiftF3: return cmFindPreviousInViewer;
	default: break;
	}

	if (key.textLength != 1 || (key.controlKeyState & (kbCtrlShift | kbAltShift)))
		return std::nullopt;
	const char c = key.text[0];
	if (c == 'W') // Shift+W
		return cmToggleLineWrap;
	if (c >= '1' && c < '1' + viewAsCount)
		return cmViewAs(ViewAs(c - '1'));
	return std::nullopt;
}

} // namespace

// The file's text, or its bytes in hex, scrolled by rows. Text rows wrap at the view's width unless wrapping is off.
class CTuiViewerContent final : public TView
{
public:
	CTuiViewerContent(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept :
		TView(bounds),
		_verticalScrollBar(verticalScrollBar)
	{
		options |= ofSelectable;
		growMode = gfGrowHiX | gfGrowHiY;
		eventMask |= evBroadcast | evMouseWheel;
	}

	// Called after the view scrolls or shows the file another way
	std::function<void()> onScrolled;

	// As text in the encoding detected, from the start
	void showFile(QByteArray data)
	{
		_data = std::move(data);
		_topRow = 0;
		showAs(ViewAs::DetectedEncoding);
	}

	// False, changing nothing, when the file is not text in that encoding. The same share of the rows stays above the top row.
	bool showAs(ViewAs viewAs);
	void setWrapped(bool wrapped);

	[[nodiscard]] ViewAs viewAs() const noexcept { return _viewAs; }
	[[nodiscard]] bool wrapped() const noexcept { return _wrapped; }
	// Empty in hex
	[[nodiscard]] const QString& encodingName() const noexcept { return _encodingName; }
	// The top row's line or offset, and the share of the rows down to the bottom one
	[[nodiscard]] QString positionText() const;

	// Forward: the first match after the current one's start, else from the top row's start; backward: the last before either.
	// Past the end, continues once from the other end. Not found: the current match stays.
	FindOutcome find(const SearchSpec& spec, const QRegularExpression& regex, bool backward);
	// The current match's line or offset. Requires a match.
	[[nodiscard]] QString matchPlaceText() const;

	void draw() override;
	void handleEvent(TEvent& event) override;
	void changeBounds(const TRect& bounds) override;

private:
	[[nodiscard]] bool isHex() const noexcept { return _viewAs == ViewAs::Hex; }
	[[nodiscard]] qsizetype rowCount() const noexcept;
	// A character's offset in text, a byte's in hex
	[[nodiscard]] qsizetype rowStart(qsizetype row) const noexcept;
	[[nodiscard]] qsizetype rowContaining(qsizetype offset) const noexcept;
	// Text: the cells before `offset` in its row
	[[nodiscard]] qsizetype columnOf(qsizetype offset) const;

	// Fits the rows to the view's width
	void layOutRows();
	void scrollTo(qsizetype topRow, qsizetype leftColumn);
	void scrollToShow(const Match& match);

	// The text a search runs over: in hex, the bytes as Latin-1, converted once
	[[nodiscard]] const QString& haystack();
	[[nodiscard]] bool isInMatch(const qsizetype offset) const noexcept { return _match && offset >= _match->start && offset < _match->end(); }

	void drawTextRow(TDrawBuffer& line, qsizetype row, TColorAttr textColor, TColorAttr matchColor) const;
	void drawHexRow(TDrawBuffer& line, qsizetype row, TColorAttr textColor, TColorAttr offsetColor, TColorAttr barColor, TColorAttr matchColor) const;

private:
	QByteArray _data;
	QString _text; // Text: the decoded file. Hex: empty until haystack() fills it.
	QString _encodingName;
	ViewAs _viewAs = ViewAs::DetectedEncoding;
	bool _wrapped = true;

	std::vector<qsizetype> _rowStarts = { 0 }; // Text: each row's first offset, then the text's size
	std::vector<qsizetype> _rowLines; // Text: the 0-based line each row belongs to
	qsizetype _widestRowCells = 0; // Text: how far the view scrolls right without wrapping
	int _bytesPerRow = 16; // Hex

	qsizetype _topRow = 0;
	qsizetype _leftColumn = 0; // Text without wrapping
	std::optional<Match> _match;
	TScrollBar* const _verticalScrollBar;
};

bool CTuiViewerContent::showAs(const ViewAs viewAs)
{
	DecodedText decodedText;
	if (viewAs != ViewAs::Hex)
	{
		std::optional<DecodedText> decodedFile = decoded(_data, viewAs);
		if (!decodedFile)
			return false;
		decodedText = std::move(*decodedFile);
	}

	// Rows hold other offsets in another encoding
	const double topShare = rowCount() > 0 ? (double)_topRow / (double)rowCount() : 0.0;
	_viewAs = viewAs;
	_text = std::move(decodedText.text);
	_encodingName = std::move(decodedText.encodingName);
	_match.reset();
	layOutRows();
	scrollTo((qsizetype)(topShare * (double)rowCount()), 0);
	return true;
}

void CTuiViewerContent::setWrapped(const bool wrapped)
{
	const qsizetype topOffset = rowStart(_topRow);
	_wrapped = wrapped;
	layOutRows();
	scrollTo(rowContaining(topOffset), 0);
}

QString CTuiViewerContent::positionText() const
{
	const qsizetype rows = rowCount();
	if (rows == 0)
		return QStringLiteral("Empty");

	const qsizetype percent = std::min(_topRow + size.y, rows) * 100 / rows;
	if (isHex())
		return QStringLiteral("Offset 0x%1 of 0x%2  %3%").arg(QString::number(rowStart(_topRow), 16).toUpper(), QString::number(_data.size(), 16).toUpper()).arg(percent);
	return QStringLiteral("Line %1 of %2  %3%").arg(_rowLines[(size_t)_topRow] + 1).arg(_rowLines.back() + 1).arg(percent);
}

FindOutcome CTuiViewerContent::find(const SearchSpec& spec, const QRegularExpression& regex, const bool backward)
{
	const QString& text = haystack();
	const qsizetype from = _match ? _match->start + (backward ? 0 : 1) : rowStart(_topRow);
	const auto search = [&](qsizetype position) {
		return backward ? lastMatchBefore(text, spec, regex, position, isHex()) : firstMatchFrom(text, spec, regex, position, isHex());
	};

	FindOutcome outcome = FindOutcome::Found;
	std::optional<Match> match = search(from);
	if (!match)
	{
		match = search(backward ? text.size() : 0);
		outcome = FindOutcome::FoundAfterWrapAround;
	}
	if (!match)
		return FindOutcome::NotFound;

	_match = match;
	scrollToShow(*match);
	return outcome;
}

QString CTuiViewerContent::matchPlaceText() const
{
	assert_and_return_r(_match, {});
	if (isHex())
		return QStringLiteral("offset 0x%1").arg(QString::number(_match->start, 16).toUpper());
	return QStringLiteral("line %1").arg(_rowLines[(size_t)rowContaining(_match->start)] + 1);
}

void CTuiViewerContent::draw()
{
	const TColorAttr textColor = mapColor(ppFileListRow), matchColor = mapColor(ppFileListCursor);
	for (int y = 0; y < size.y; ++y)
	{
		TDrawBuffer line;
		line.moveChar(0, ' ', textColor, (ushort)size.x);
		if (const qsizetype row = _topRow + y; row < rowCount())
		{
			if (isHex())
				drawHexRow(line, row, textColor, mapColor(ppHexOffset), mapColor(ppColumnSeparator), matchColor);
			else
				drawTextRow(line, row, textColor, matchColor);
		}
		writeLine(0, (short)y, (short)size.x, 1, line);
	}
}

void CTuiViewerContent::handleEvent(TEvent& event)
{
	TView::handleEvent(event);

	switch (event.what)
	{
	case evMouseWheel:
		switch (event.mouse.wheel)
		{
		case mwUp:    scrollTo(_topRow - wheelStep, _leftColumn); break;
		case mwDown:  scrollTo(_topRow + wheelStep, _leftColumn); break;
		case mwLeft:  scrollTo(_topRow, _leftColumn - wheelStep); break;
		case mwRight: scrollTo(_topRow, _leftColumn + wheelStep); break;
		default: return;
		}
		clearEvent(event);
		break;
	case evKeyDown:
	{
		const qsizetype pageStep = std::max(size.y - 1, 1);
		switch (event.keyDown.keyCode)
		{
		case kbUp:   scrollTo(_topRow - 1, _leftColumn); break;
		case kbDown: scrollTo(_topRow + 1, _leftColumn); break;
		case kbPgUp: scrollTo(_topRow - pageStep, _leftColumn); break;
		case kbPgDn: scrollTo(_topRow + pageStep, _leftColumn); break;
		case kbLeft:  scrollTo(_topRow, _leftColumn - 1); break;
		case kbRight: scrollTo(_topRow, _leftColumn + 1); break;
		case kbHome: case kbCtrlHome: scrollTo(0, 0); break;
		case kbEnd:  case kbCtrlEnd:  scrollTo(rowCount(), _leftColumn); break;
		default: return;
		}
		clearEvent(event);
		break;
	}
	case evBroadcast:
		if (event.message.command == cmScrollBarChanged && event.message.infoPtr == _verticalScrollBar && _verticalScrollBar->value != std::min<qsizetype>(_topRow, INT_MAX))
			scrollTo(_verticalScrollBar->value, _leftColumn);
		break;
	default:
		break;
	}
}

void CTuiViewerContent::changeBounds(const TRect& bounds)
{
	const qsizetype topOffset = rowStart(_topRow);
	const bool widthChanged = bounds.b.x - bounds.a.x != size.x;
	TView::changeBounds(bounds);
	if (widthChanged && (isHex() || _wrapped))
		layOutRows();
	scrollTo(rowContaining(topOffset), _leftColumn);
}

qsizetype CTuiViewerContent::rowCount() const noexcept
{
	return isHex() ? (_data.size() + _bytesPerRow - 1) / _bytesPerRow : (qsizetype)_rowStarts.size() - 1;
}

qsizetype CTuiViewerContent::rowStart(const qsizetype row) const noexcept
{
	return isHex() ? row * _bytesPerRow : _rowStarts[(size_t)row];
}

qsizetype CTuiViewerContent::rowContaining(const qsizetype offset) const noexcept
{
	if (isHex())
		return offset / _bytesPerRow;
	return std::max<qsizetype>(std::upper_bound(_rowStarts.begin(), _rowStarts.end() - 1, offset) - _rowStarts.begin() - 1, 0);
}

qsizetype CTuiViewerContent::columnOf(const qsizetype offset) const
{
	qsizetype column = 0;
	for (qsizetype i = rowStart(rowContaining(offset)); i < offset;)
	{
		const CharLayout layout = charLayout(_text.constData(), _text.size(), i, column);
		column += layout.cells;
		i += layout.length;
	}
	return column;
}

void CTuiViewerContent::layOutRows()
{
	if (isHex())
	{
		_bytesPerRow = hexBytesPerRow(size.x, hexOffsetDigits(_data.size()));
		return;
	}

	_rowStarts.assign(1, 0);
	_rowLines.assign(1, 0);
	_widestRowCells = 0;

	const QChar* const chars = _text.constData();
	const qsizetype textSize = _text.size();
	const qsizetype maxRowCells = _wrapped ? std::max(size.x, 1) : std::numeric_limits<qsizetype>::max();
	qsizetype line = 0, rowBegin = 0, rowCells = 0;
	// The last space or tab the row may break after, and the row's cells before it
	qsizetype breakAfter = -1, cellsBeforeBreak = 0;

	const auto endRow = [&](const qsizetype endedRowCells, const qsizetype nextRowStart, const bool lineEnds) {
		_widestRowCells = std::max(_widestRowCells, endedRowCells);
		if (lineEnds)
			++line;
		_rowStarts.push_back(nextRowStart);
		_rowLines.push_back(line);
		rowBegin = nextRowStart;
		rowCells = 0;
		breakAfter = -1;
	};

	for (qsizetype i = 0; i < textSize;)
	{
		const CharLayout layout = charLayout(chars, textSize, i, rowCells);
		rowCells += layout.cells;

		if (chars[i] == u'\n')
		{
			endRow(rowCells, i + 1, true);
			++i;
			continue;
		}

		if (chars[i] == u' ' || chars[i] == u'\t')
		{
			breakAfter = i;
			cellsBeforeBreak = rowCells - layout.cells;
		}

		if (rowCells > maxRowCells && i > rowBegin)
		{
			// Either way the next row measures its characters again: a tab's cells depend on the column it starts at
			if (breakAfter > rowBegin)
			{
				const qsizetype nextRowStart = breakAfter + 1; // The space or tab stays at the end of the row it breaks
				endRow(cellsBeforeBreak, nextRowStart, false);
				i = nextRowStart;
			}
			else
				endRow(rowCells - layout.cells, i, false);
			continue;
		}

		i += layout.length;
	}

	_widestRowCells = std::max(_widestRowCells, rowCells);
	if (_rowStarts.back() != textSize)
		_rowStarts.push_back(textSize);
	// A text ending in a line break started a row there, which the end takes the place of
	_rowLines.resize(_rowStarts.size() - 1);
}

void CTuiViewerContent::scrollTo(const qsizetype topRow, const qsizetype leftColumn)
{
	const int visibleRows = std::max(size.y, 1);
	const qsizetype maxTopRow = std::max<qsizetype>(rowCount() - visibleRows, 0);
	_topRow = std::clamp<qsizetype>(topRow, 0, maxTopRow);
	_leftColumn = (isHex() || _wrapped) ? 0 : std::clamp<qsizetype>(leftColumn, 0, std::max<qsizetype>(_widestRowCells - size.x, 0));

	// The scroll bar counts in int: rows past INT_MAX are reached by keys only
	_verticalScrollBar->setParams((int)std::min<qsizetype>(_topRow, INT_MAX), 0, (int)std::min<qsizetype>(maxTopRow, INT_MAX), visibleRows - 1, 1);
	drawView();
	if (onScrolled)
		onScrolled();
}

void CTuiViewerContent::scrollToShow(const Match& match)
{
	const qsizetype row = rowContaining(match.start);
	qsizetype topRow = _topRow;
	if (row < _topRow)
		topRow = row;
	else if (row >= _topRow + size.y)
		topRow = row - size.y + 1;

	qsizetype leftColumn = _leftColumn;
	if (!isHex() && !_wrapped)
	{
		if (const qsizetype column = columnOf(match.start); column < _leftColumn || column >= _leftColumn + size.x)
			leftColumn = column - size.x / 3;
	}
	scrollTo(topRow, leftColumn);
}

const QString& CTuiViewerContent::haystack()
{
	if (isHex() && _text.isEmpty())
		_text = QString::fromLatin1(_data);
	return _text;
}

// Draws the cells from _leftColumn on. A character cut by the left edge shows as blank cells.
void CTuiViewerContent::drawTextRow(TDrawBuffer& line, const qsizetype row, const TColorAttr textColor, const TColorAttr matchColor) const
{
	const QChar* const chars = _text.constData();
	const qsizetype textSize = _text.size(), rowEnd = _rowStarts[(size_t)row + 1], viewEnd = _leftColumn + size.x;

	// A run of characters in one colour, drawn at once: Turbo Vision joins a combining character to the one before it in the same string
	QString segment;
	int segmentX = 0;
	bool segmentInMatch = false;
	const auto drawSegmentAndStartAt = [&](const int nextX) {
		if (!segment.isEmpty())
		{
			line.moveStr((ushort)segmentX, stringView(segment.toUtf8()), segmentInMatch ? matchColor : textColor, (ushort)(size.x - segmentX));
			segment.clear();
		}
		segmentX = nextX;
	};

	qsizetype column = 0;
	for (qsizetype i = _rowStarts[(size_t)row]; i < rowEnd && column < viewEnd;)
	{
		const CharLayout layout = charLayout(chars, textSize, i, column);
		const qsizetype charEnd = column + layout.cells;
		if (column >= _leftColumn || charEnd > _leftColumn)
		{
			const int x = (int)(std::max(column, _leftColumn) - _leftColumn);
			if (const bool inMatch = isInMatch(i); inMatch != segmentInMatch || segment.isEmpty())
			{
				drawSegmentAndStartAt(x);
				segmentInMatch = inMatch;
			}

			if (column < _leftColumn || chars[i] == u'\t')
				segment += QString(charEnd - std::max(column, _leftColumn), u' ');
			else if (!isLineBreak(chars[i], layout))
				segment.append(chars + i, layout.length);
		}
		column = charEnd;
		i += layout.length;
	}
	drawSegmentAndStartAt(0);
}

void CTuiViewerContent::drawHexRow(TDrawBuffer& line, const qsizetype row, const TColorAttr textColor, const TColorAttr offsetColor, const TColorAttr barColor, const TColorAttr matchColor) const
{
	static constexpr char hexDigits[] = "0123456789ABCDEF";
	const int offsetDigits = hexOffsetDigits(_data.size());
	const qsizetype start = rowStart(row), end = std::min(start + _bytesPerRow, _data.size());

	line.moveStr(0, stringView(QByteArray::number(start, 16).toUpper().rightJustified(offsetDigits, '0')), offsetColor);
	line.moveStr((ushort)hexBarColumn(offsetDigits, _bytesPerRow), "│", barColor);
	for (qsizetype offset = start; offset < end; ++offset)
	{
		const int index = (int)(offset - start);
		const auto byte = (uchar)_data[offset];
		const TColorAttr color = isInMatch(offset) ? matchColor : textColor;
		const char hex[]{ hexDigits[byte >> 4], hexDigits[byte & 0xF] };
		line.moveStr((ushort)hexByteColumn(offsetDigits, index), TStringView{ hex, 2 }, color);
		const char ascii = byte >= 0x20 && byte < 0x7F ? (char)byte : '.';
		line.moveStr((ushort)(hexAsciiColumn(offsetDigits, _bytesPerRow) + index), TStringView{ &ascii, 1 }, color);
	}
}

// The viewer's bottom row: how the file is shown and the last command's outcome on the left, the position on the right
class CTuiViewerStatusLine final : public TView
{
public:
	explicit CTuiViewerStatusLine(const TRect& bounds) noexcept : TView(bounds)
	{
		growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
	}

	void setText(const QString& left, const QString& right)
	{
		_left = left.toUtf8();
		_right = right.toUtf8();
		drawView();
	}

	void draw() override
	{
		const TColorAttr color = mapColor(ppStatusText);
		const int rightWidth = (int)TText::width(stringView(_right));
		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		line.moveStr(1, stringView(_left), color, (ushort)std::max(size.x - rightWidth - 3, 0));
		line.moveStr((ushort)std::max(size.x - rightWidth - 1, 0), stringView(_right), color);
		writeLine(0, 0, (short)size.x, 1, line);
	}

private:
	QByteArray _left;
	QByteArray _right;
};

CTuiFileView::CTuiFileView(const TRect& bounds, TScrollBar* verticalScrollBar) :
	TGroup(bounds)
{
	growMode = gfGrowHiX | gfGrowHiY;

	_statusLine = new CTuiViewerStatusLine({ 0, size.y - 1, size.x, size.y });
	insert(_statusLine);
	_content = new CTuiViewerContent({ 0, 0, size.x, size.y - 1 }, verticalScrollBar);
	_content->onScrolled = [this] { updateStatusLine(); };
	insert(_content);
	_content->select();
}

void CTuiFileView::showFile(QByteArray data, QString note)
{
	_unreadable = false;
	_message = std::move(note);
	_content->showFile(std::move(data));
}

void CTuiFileView::showError(QString message)
{
	_unreadable = true;
	_message = std::move(message);
	_content->showFile({});
}

void CTuiFileView::showAs(const ViewAs viewAs)
{
	const bool shown = _content->showAs(viewAs);
	_message = shown ? QString{} : QStringLiteral("The file is not text in this encoding");
	updateStatusLine();
}

void CTuiFileView::toggleWrap()
{
	_message.clear();
	_content->setWrapped(!_content->wrapped());
}

void CTuiFileView::find(const SearchSpec& search, const bool backward)
{
	const QRegularExpression regex = regexFor(search);
	if (!regex.isValid())
		_message = QStringLiteral("Invalid pattern: %1").arg(regex.errorString());
	else switch (_content->find(search, regex, backward))
	{
	case FindOutcome::Found:
		_message = QStringLiteral("Found at %1").arg(_content->matchPlaceText());
		break;
	case FindOutcome::FoundAfterWrapAround:
		// The GUI's find bar's wording
		_message = QStringLiteral("Search restarted from the %1: found at %2").arg(backward ? QStringLiteral("bottom") : QStringLiteral("top"), _content->matchPlaceText());
		break;
	case FindOutcome::NotFound:
		_message = QStringLiteral("Not found");
		break;
	}
	updateStatusLine();
}

void CTuiFileView::updateStatusLine()
{
	if (_unreadable)
	{
		_statusLine->setText(_message, {});
		return;
	}

	QString shownAs;
	if (_content->viewAs() == ViewAs::Hex)
		shownAs = QStringLiteral("Hex");
	else
	{
		shownAs = _content->encodingName();
		shownAs += _content->wrapped() ? QStringLiteral(" · Wrap") : QStringLiteral(" · No wrap");
	}
	if (!_message.isEmpty())
	{
		shownAs += QStringLiteral("  ");
		shownAs += _message;
	}
	_statusLine->setText(shownAs, _content->positionText());
}

// Shows one file above the bottom rows until Esc. Its help context puts its keys on the hint row and the key bar.
class CTuiViewerWindow final : public CTuiPathTitledWindow
{
public:
	CTuiViewerWindow(const TRect& bounds, const QString& filePath) :
		TWindowInit(&TWindow::initFrame),
		CTuiPathTitledWindow(bounds, stringView(toNativeSeparators(filePath).toUtf8())),
		_search(savedSearchOptions())
	{
		flags &= ~(wfMove | wfGrow | wfZoom);
		growMode = gfGrowHiX | gfGrowHiY;
		state &= ~sfShadow; // It fills the screen above the bottom rows
		helpCtx = hcViewer;

		TRect interior = getExtent();
		interior.grow(-1, -1);
		_fileView = new CTuiFileView(interior, standardScrollBar(sbVertical));
		insert(_fileView);
		_fileView->select();
	}

	void showFile(QByteArray data) { _fileView->showFile(std::move(data)); }

	void handleEvent(TEvent& event) override
	{
		TWindow::handleEvent(event);

		std::optional<ushort> command;
		if (event.what == evKeyDown)
			command = commandForKey(event.keyDown);
		else if (event.what == evCommand)
			command = event.message.command;

		if (command && runCommand(*command))
			clearEvent(event);
	}

	// The text in the file list's colours
	TPalette& getPalette() const override { return panelPalette(); }

private:
	// False for a command the viewer does not carry out
	bool runCommand(const ushort command)
	{
		if (command >= cmFirstViewAs && command < cmFirstViewAs + viewAsCount)
		{
			_fileView->showAs(ViewAs(command - cmFirstViewAs));
			return true;
		}

		switch (command)
		{
		case cmCancel:
			endModal(cmCancel);
			return true;
		case cmFindInViewer:
			if (std::optional<SearchSpec> search = askForSearch(_search))
			{
				_search = std::move(*search);
				_fileView->find(_search, false);
			}
			return true;
		case cmFindNextInViewer:
		case cmFindPreviousInViewer:
			if (_search.text.isEmpty())
				return runCommand(cmFindInViewer);
			_fileView->find(_search, command == cmFindPreviousInViewer);
			return true;
		case cmToggleLineWrap:
			_fileView->toggleWrap();
			return true;
		default:
			return false;
		}
	}

private:
	CTuiFileView* _fileView; // Owned by this window
	SearchSpec _search; // No text until the first search
};

CTuiQuickView::CTuiQuickView(const TPoint desktopSize, const Panel side) :
	TWindowInit(&TWindow::initFrame),
	TWindow(panelBounds(desktopSize, side), "Quick view", wnNoNumber),
	_side(side)
{
	flags &= ~(wfMove | wfGrow | wfClose | wfZoom); // The application ends it
	options &= ~ofSelectable;
	state &= ~sfShadow; // Tiled: a shadow would fall on the other panel

	TRect interior = getExtent();
	interior.grow(-1, -1);
	_fileView = new CTuiFileView(interior, standardScrollBar(sbVertical));
	insert(_fileView);
}

void CTuiQuickView::calcBounds(TRect& bounds, TPoint)
{
	bounds = panelBounds(owner->size, _side);
}

void CTuiQuickView::handleEvent(TEvent& event)
{
	// Turbo Vision offers the wheel to every view regardless of the mouse's position: only the view under the mouse scrolls
	if (event.what == evMouseWheel && !mouseInView(event.mouse.where))
		return;
	TWindow::handleEvent(event);
}

TPalette& CTuiQuickView::getPalette() const
{
	return panelPalette();
}

void CTuiQuickView::showFile(const QString& filePath)
{
	if (filePath == _filePath)
		return;

	_filePath = filePath;
	setTitle(*this, QFileInfo{ filePath }.fileName()); // The folder is the active panel's

	QFile file{ filePath };
	if (!file.open(QFile::ReadOnly))
	{
		_fileView->showError(QStringLiteral("Cannot read the file: %1").arg(file.errorString()));
		return;
	}

	QByteArray data = file.read(quickViewMaxBytes);
	const bool cut = !file.atEnd();
	_fileView->showFile(std::move(data), cut ? QStringLiteral("First %1 MiB").arg(quickViewMaxBytes / (1024 * 1024)) : QString{});
}

void viewFile(const QString& filePath)
{
	QFile file{ filePath };
	if (!file.open(QFile::ReadOnly))
	{
		showMessage(QStringLiteral("Failed to open the file"), toNativeSeparators(filePath) + u'\n' + file.errorString());
		return;
	}

	// Over the menu bar too: a modal view leaves its menus unresponsive
	TRect bounds = TProgram::application->getExtent();
	bounds.b.y -= 2; // The hint row and the key bar
	auto* window = new CTuiViewerWindow(bounds, filePath);
	try
	{
		window->showFile(file.readAll());
	}
	catch (const std::bad_alloc&)
	{
		TObject::destroy(window);
		showMessage(QStringLiteral("Failed to open the file"), QStringLiteral("The file is too large to view."));
		return;
	}

	TProgram::application->execView(window);
	TObject::destroy(window);
}
