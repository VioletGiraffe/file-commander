#include "ctuifinddialog.h"
#include "ctuiapplication.h"
#include "ctuidialogs.h"
#include "ctuiprograms.h"
#include "ctuirowlist.h"

#include "cfilesystemobject.h"
#include "filesearchengine/filesearchquery.h"
#include "filesystemhelperfunctions.h"
#include "settings.h"

#include "utils/naturalsorting/cnaturalsorterqcollator.h"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_THistory
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TSItem
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdint.h>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr ushort cmStartOrStopSearch = 1200, cmGoToResult = 1201;
constexpr uchar nameHistoryId = 1, contentsHistoryId = 2, searchInHistoryId = 3;

// The check boxes' items, in their order
enum NameOption { PartialMatch, NameCaseSensitive };
enum ContentsOption { WholeWords, ContentsIsRegex, ContentsCaseSensitive };

// Turbo Vision keeps the histories of all fields in one 1 KiB block, and drops the oldest entries when it is full
constexpr qsizetype maxHistoryEntriesShown = 8;
// Turbo Vision stores an entry's length plus 3 in one byte: a longer entry corrupts the history
constexpr qsizetype maxHistoryEntryBytes = 252;
// In bytes. A "Search in" text holds the path of every item selected.
constexpr int inputLimit = 32 * 1024;
constexpr auto resultsDisplayInterval = 100ms;
constexpr size_t maxResultsPerDisplay = 10'000;

// "1 item", "2 items"
[[nodiscard]] QString itemCountText(const uint64_t count)
{
	return count == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(count);
}

[[nodiscard]] QString inputText(const TInputLine* input)
{
	return QString::fromUtf8(input->data);
}

void setInputText(TInputLine* input, const QString& text)
{
	strnzcpy(input->data, stringView(text.toUtf8()), (size_t)input->maxLen + 1);
	input->selectAll(True);
}

void setChecked(TCheckBoxes* checkBoxes, std::initializer_list<bool> checked)
{
	ushort bits = 0;
	for (int item = 0; const bool itemChecked : checked)
		bits |= (ushort)(itemChecked << item++);
	checkBoxes->setData(&bits);
}

[[nodiscard]] bool isChecked(TCheckBoxes* checkBoxes, int item)
{
	return checkBoxes->mark(item) != False;
}

// Leaves out an entry too long for Turbo Vision's history
void addToTurboVisionHistory(uchar historyId, const QByteArray& utf8)
{
	if (utf8.size() <= maxHistoryEntryBytes)
		historyAdd(historyId, stringView(utf8));
}

// The newest entries that fit, added oldest first: Turbo Vision keeps a history's most recent entry last
void addNewestToTurboVisionHistory(uchar historyId, const QStringList& mostRecentFirst)
{
	std::vector<QByteArray> newestFirst;
	for (qsizetype i = 0; i < mostRecentFirst.size() && (qsizetype)newestFirst.size() < maxHistoryEntriesShown; ++i)
	{
		if (QByteArray utf8 = mostRecentFirst[i].toUtf8(); utf8.size() <= maxHistoryEntryBytes)
			newestFirst.push_back(std::move(utf8));
	}

	for (auto entry = newestFirst.rbegin(); entry != newestFirst.rend(); ++entry)
		addToTurboVisionHistory(historyId, *entry);
}

// Moves the text to the front of the history and saves it, as the GUI's history fields do. Empty text is not recorded.
void recordInHistory(QStringList& history, const QString& text, const QString& settingsKey, uchar historyId)
{
	if (text.isEmpty())
		return;

	history.removeAll(text);
	history.push_front(text);
	if (history.size() > FILE_SEARCH_HISTORY_MAX_ENTRIES)
		history.resize(FILE_SEARCH_HISTORY_MAX_ENTRIES);
	QSettings{}.setValue(settingsKey, history);
	addToTurboVisionHistory(historyId, text.toUtf8());
}

// Never records its field's text, which THistory does when its list opens and on any button press: the dialog records each search's texts itself
class CTuiFieldHistory final : public THistory
{
public:
	using THistory::THistory;

	void recordHistory(const char* /*text*/) override {}
};

struct FoundItem
{
	QString path; // CFileSystemObject::fullAbsolutePath()
	QString text; // As shown
	bool isFile = false;
	bool reachedThroughLink = false;
};

[[nodiscard]] bool pathOrderLess(const QString& l, const QString& r)
{
	return NaturalSort::lessThan(l, r);
}

[[nodiscard]] bool resultOrderLess(const FoundItem& l, const FoundItem& r)
{
	return pathOrderLess(l.path, r.path);
}

} // namespace

// The results: in the order found while the search runs, then sorted. A link mark starts the row of an item found through a directory link.
class CTuiFindResultsList final : public CTuiRowList
{
public:
	CTuiFindResultsList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept : CTuiRowList(bounds, verticalScrollBar) {}

	void clear()
	{
		_items.clear();
		rowsChanged(0);
	}

	// The cursor follows the newest item while it is on the last row
	void append(std::vector<FoundItem> items)
	{
		if (items.empty())
			return;

		const bool followingNewest = cursorIsOnLastRow();
		_items.insert(_items.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
		rowsChanged(followingNewest ? (int)_items.size() - 1 : cursorRow());
	}

	// In the panel's natural order of their paths. The cursor stays on its item, or goes from the last row to the first.
	void sort()
	{
		if (_items.empty())
			return;

		const bool followingNewest = cursorIsOnLastRow();
		const QString cursorPath = _items[(size_t)cursorRow()].path;
		std::ranges::sort(_items, resultOrderLess);
		rowsChanged(followingNewest ? 0 : (int)(std::ranges::lower_bound(_items, cursorPath, pathOrderLess, &FoundItem::path) - _items.begin()));
	}

	void handleEvent(TEvent& event) override
	{
		CTuiRowList::handleEvent(event);
		// A dialog's buttons and check boxes also take their letters without Alt, from a focused view that leaves them
		if (event.what == evKeyDown && event.keyDown.textLength > 0 && (uchar)event.keyDown.text[0] >= ' ' && !(event.keyDown.controlKeyState & (kbAltShift | kbCtrlShift)))
			clearEvent(event);
	}

	[[nodiscard]] const FoundItem* currentItem() const noexcept { return _items.empty() ? nullptr : &_items[(size_t)cursorRow()]; }
	[[nodiscard]] size_t itemCount() const noexcept { return _items.size(); }

	void draw() override
	{
		const TColorAttr normalColor = mapColor(1), cursorColor = mapColor(2);
		for (int y = 0; y < size.y; ++y)
		{
			const int row = topRow() + y;
			const TColorAttr color = cursorIsShownOn(row) ? cursorColor : normalColor;

			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			if (row < (int)_items.size())
			{
				const FoundItem& item = _items[(size_t)row];
				if (item.reachedThroughLink)
					line.moveStr(0, "↪", color);
				line.moveStr(1, stringView(elidedInTheMiddle(item.text, size.x - 2).toUtf8()), color, (ushort)std::max(size.x - 2, 0));
			}
			writeLine(0, (short)y, (short)size.x, 1, line);
		}
	}

	TPalette& getPalette() const override
	{
		static TPalette palette("\x1A\x1B", 2); // The dialog palette's list viewer entries: normal, focused
		return palette;
	}

private:
	[[nodiscard]] int rowCount() const noexcept override { return (int)_items.size(); }
	// True for an empty list
	[[nodiscard]] bool cursorIsOnLastRow() const noexcept { return cursorRow() >= (int)_items.size() - 1; }

private:
	std::vector<FoundItem> _items;
};

CTuiFindDialog::CTuiFindDialog(const TRect& bounds) :
	TWindowInit(&TDialog::initFrame),
	TDialog(bounds, "Find files")
{
	flags &= ~wfMove;
	growMode = gfGrowHiX | gfGrowHiY;

	static constexpr int labelX = 2, fieldX = 18, listTop = 11, buttonWidth = 12;
	const int right = size.x - 2, statusY = size.y - 2;

	// A labelled input line with its history, from fieldX to fieldEnd
	const auto addField = [this](int y, int fieldEnd, const char* label, uchar historyId) {
		const int historyX = fieldEnd - 3;
		auto* input = new TInputLine({ fieldX, y, historyX, y + 1 }, inputLimit);
		input->growMode = gfGrowHiX;
		insert(input);
		insert(new TLabel({ labelX, y, fieldX - 1, y + 1 }, label, input));
		auto* history = new CTuiFieldHistory({ historyX, y, fieldEnd, y + 1 }, input, historyId);
		history->growMode = gfGrowLoX | gfGrowHiX;
		insert(history);
		return input;
	};

	_name = addField(2, right, "Search ~f~or:", nameHistoryId);
	_nameOptions = new TCheckBoxes({ fieldX, 3, right, 4 }, new TSItem("~P~artial match", new TSItem("Case sensi~t~ive", nullptr)));
	_nameOptions->growMode = gfGrowHiX;
	insert(_nameOptions);

	_contents = addField(5, right, "With c~o~ntents:", contentsHistoryId); // Alt+C copies a result's path, as in the panel
	_contentsOptions = new TCheckBoxes({ fieldX, 6, right, 7 }, new TSItem("~W~hole words", new TSItem("~R~egex", new TSItem("Case sensiti~v~e", nullptr))));
	_contentsOptions->growMode = gfGrowHiX;
	insert(_contentsOptions);

	const int buttonX = right - buttonWidth;
	_searchIn = addField(8, buttonX - 1, "Search ~i~n:", searchInHistoryId);
	_searchButton = new TButton({ buttonX, 8, right, 10 }, "~S~earch", cmStartOrStopSearch, bfDefault);
	_searchButton->growMode = gfGrowLoX | gfGrowHiX;
	insert(_searchButton);

	auto* scrollBar = new TScrollBar({ right - 1, listTop, right, statusY });
	scrollBar->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
	insert(scrollBar);
	_results = new CTuiFindResultsList({ labelX, listTop, right - 1, statusY }, scrollBar);
	insert(_results);

	_status = new CTuiTextView({ labelX, statusY, right, statusY + 1 }, {});
	_status->growMode = gfGrowLoY | gfGrowHiY | gfGrowHiX;
	insert(_status);

	const QSettings settings;
	_nameHistory = settings.value(KEY_FILE_SEARCH_NAME_HISTORY).toStringList();
	_contentsHistory = settings.value(KEY_FILE_SEARCH_CONTENTS_HISTORY).toStringList();
	_searchInHistory = settings.value(KEY_FILE_SEARCH_ROOT_HISTORY).toStringList();
	addNewestToTurboVisionHistory(nameHistoryId, _nameHistory);
	addNewestToTurboVisionHistory(contentsHistoryId, _contentsHistory);
	addNewestToTurboVisionHistory(searchInHistoryId, _searchInHistory);

	// The contents field starts empty: a pattern left in it would silently restrict the search
	setInputText(_name, _nameHistory.value(0));
	setChecked(_nameOptions, { settings.value(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, FILE_SEARCH_NAME_PARTIAL_MATCH_DEFAULT).toBool(),
		settings.value(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, false).toBool() });
	setChecked(_contentsOptions, { settings.value(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, false).toBool(),
		settings.value(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, false).toBool(), settings.value(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, false).toBool() });
}

std::optional<QString> CTuiFindDialog::exec(const QString& searchInText, const QString& baseDir)
{
	if (const TRect desktopArea = TProgram::deskTop->getExtent(); getBounds() != desktopArea)
		changeBounds(desktopArea);

	setInputText(_searchIn, searchInText);
	_baseDir = baseDir;
	_name->select();

	TProgram::deskTop->execView(this);

	if (_searchActive)
	{
		_engine.stopSearching();
		_engine.waitForSearchToFinish();
		showPendingResults(SIZE_MAX); // Everything the search reported, its completion included
	}

	saveOptions();
	return std::exchange(_chosenPath, std::nullopt);
}

void CTuiFindDialog::getEvent(TEvent& event)
{
	if (_searchActive && std::chrono::steady_clock::now() >= _nextResultsDisplayTime)
		showPendingResults(maxResultsPerDisplay);

	TDialog::getEvent(event);
}

void CTuiFindDialog::handleEvent(TEvent& event)
{
	// Before the base class: it gives Enter to the default button, and turns Esc into closing the dialog
	if (event.what == evKeyDown)
	{
		const bool inResults = (_results->state & sfFocused) != 0;
		const ushort key = event.keyDown.keyCode;
		if (key == kbEnter && inResults)
		{
			goToCurrentResult();
			clearEvent(event);
			return;
		}
		if (key == kbEsc && _searchActive)
		{
			_engine.stopSearching();
			clearEvent(event);
			return;
		}
		if ((key == kbF3 || key == kbF4) && inResults)
		{
			if (const FoundItem* item = _results->currentItem(); item && item->isFile)
				key == kbF3 ? viewFile(item->path) : editFile(item->path);
			clearEvent(event);
			return;
		}
		if (key == kbAltC)
		{
			if (const FoundItem* item = _results->currentItem())
				CTuiApplication::copyToClipboard(shellQuotedPath(toNativeSeparators(item->path)));
			clearEvent(event);
			return;
		}
	}

	TDialog::handleEvent(event);

	if (event.what != evCommand)
		return;

	switch (event.message.command)
	{
	case cmStartOrStopSearch:
		_searchActive ? _engine.stopSearching() : startSearch();
		clearEvent(event);
		break;
	case cmActivateRowUnderCursor:
		goToCurrentResult();
		clearEvent(event);
		break;
	default:
		break;
	}
}

void CTuiFindDialog::startSearch()
{
	const QString nameText = inputText(_name), contentsText = inputText(_contents), searchInText = inputText(_searchIn);
	const FileSearchQuery query{
		.roots = rootsFromSearchInField(searchInText, _baseDir),
		.nameFilters = nameFiltersFromSearchField(nameText, isChecked(_nameOptions, PartialMatch)),
		.nameCaseSensitive = isChecked(_nameOptions, NameCaseSensitive),
		.contents = contentsText,
		.contentsCaseSensitive = isChecked(_contentsOptions, ContentsCaseSensitive),
		.contentsWholeWords = isChecked(_contentsOptions, WholeWords),
		.contentsIsRegex = isChecked(_contentsOptions, ContentsIsRegex)
	};

	_searchRootPrefix.clear();
	if (query.roots.size() == 1)
	{
		if (const CFileSystemObject root{ query.roots.front() }; root.isDir())
		{
			_searchRootPrefix = root.fullAbsolutePath();
			if (!_searchRootPrefix.endsWith(u'/'))
				_searchRootPrefix += u'/';
		}
	}

	if (!_engine.search(query, &_pendingResults))
	{
		setStatus(QStringLiteral("Nothing to search in"));
		return;
	}

	recordInHistory(_nameHistory, nameText, KEY_FILE_SEARCH_NAME_HISTORY, nameHistoryId);
	recordInHistory(_contentsHistory, contentsText, KEY_FILE_SEARCH_CONTENTS_HISTORY, contentsHistoryId);
	if (searchInFieldTextBelongsInHistory(searchInText))
		recordInHistory(_searchInHistory, searchInText, KEY_FILE_SEARCH_ROOT_HISTORY, searchInHistoryId);

	_results->clear();
	setStatus({});
	setSearchActive(true);
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + resultsDisplayInterval;
}

void CTuiFindDialog::showPendingResults(const size_t maxMatches)
{
	const CFileSearchResultsBuffer::Batch batch = _pendingResults.take(maxMatches);

	if (!batch.lastScannedItem.isEmpty())
		setStatus(toNativeSeparators(batch.lastScannedItem));

	std::vector<FoundItem> items;
	items.reserve(batch.matches.size());
	for (const CFileSearchResultsBuffer::Match& match : batch.matches)
	{
		FoundItem& item = items.emplace_back();
		item.path = match.item.fullAbsolutePath();
		item.isFile = match.item.isFile();
		item.reachedThroughLink = match.reachedThroughLink;

		item.text = toNativeSeparators(!_searchRootPrefix.isEmpty() && item.path.startsWith(_searchRootPrefix) ? item.path.mid(_searchRootPrefix.size()) : item.path);
		if (match.item.isDir())
		{
			if (item.text.endsWith(nativeSeparator()))
				item.text.chop(1);
			item.text = u'[' + item.text + u']';
		}
	}
	_results->append(std::move(items));

	// A full batch leaves more waiting
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + (batch.matches.size() < maxMatches ? resultsDisplayInterval : 0ms);

	if (batch.completion)
		showCompletion(*batch.completion);
}

void CTuiFindDialog::showCompletion(const CFileSearchResultsBuffer::Completion& completion)
{
	setSearchActive(false);
	_results->sort();

	if (completion.status == CFileSearchEngine::SearchInvalidPattern)
	{
		setStatus(QStringLiteral("The contents pattern is not a valid regular expression"));
		return;
	}

	QString message = completion.status == CFileSearchEngine::SearchCancelled ? QStringLiteral("Search aborted") : QStringLiteral("Search completed");
	message += QStringLiteral(", %1 found").arg(itemCountText(_results->itemCount()));
	// Neither matches nor known non-matches, so saying nothing would present the results as complete
	if (completion.inconclusiveItems > 0)
		message += QStringLiteral(", %1 could not be searched").arg(itemCountText(completion.inconclusiveItems));
	message += QStringLiteral(": %1 scanned in %2 sec").arg(itemCountText(completion.itemsScanned)).arg((double)completion.msElapsed * 1e-3, 0, 'f', 1);
	setStatus(message);

	// Unless the user is already going through them. Only the user can have moved the cursor off the last row, which sort() left at the first.
	if (_results->itemCount() > 0 && !(_results->state & sfFocused))
		_results->select();
}

void CTuiFindDialog::setSearchActive(const bool active)
{
	_searchActive = active;
	delete[] const_cast<char*>(_searchButton->title);
	_searchButton->title = newStr(active ? "~S~top" : "~S~earch");
	_searchButton->drawView();
}

void CTuiFindDialog::setStatus(const QString& text)
{
	_status->setLines({ elidedInTheMiddle(text, _status->size.x) });
}

void CTuiFindDialog::goToCurrentResult()
{
	const FoundItem* item = _results->currentItem();
	if (!item)
		return;

	_chosenPath = item->path;
	endModal(cmGoToResult);
}

void CTuiFindDialog::saveOptions() const
{
	QSettings settings;
	settings.setValue(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, isChecked(_nameOptions, PartialMatch));
	settings.setValue(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, isChecked(_nameOptions, NameCaseSensitive));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, isChecked(_contentsOptions, WholeWords));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, isChecked(_contentsOptions, ContentsIsRegex));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, isChecked(_contentsOptions, ContentsCaseSensitive));
}
