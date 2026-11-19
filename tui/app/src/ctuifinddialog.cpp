#include "ctuifinddialog.h"
#include "ctuiapplication.h"
#include "ctuidialogs.h"
#include "ctuifields.h"
#include "ctuifilelist.h"
#include "ctuiprograms.h"
#include "ctuirowlist.h"
#include "ctuiviewer.h"

#include "cfilesystemobject.h"
#include "filelist/filetimetext.h"
#include "filelist/sortedfilelist.h"
#include "filesearchengine/filesearchquery.h"
#include "filesystemhelperfunctions.h"
#include "settings.h"

#include "hash/hash_functors.hpp"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TSItem
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <3rdparty/ankerl/unordered_dense.h>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <stdint.h>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr ushort cmStartOrStopSearch = 1200, cmGoToResult = 1201, cmSendResultsToPanel = 1202;

// The check boxes' items, in their order
enum NameOption { PartialMatch, NameCaseSensitive };
enum ContentsOption { WholeWords, ContentsIsRegex, ContentsCaseSensitive };

// In bytes. A "Search in" text holds the path of every item selected.
constexpr int inputLimit = 32 * 1024;
constexpr auto resultsDisplayInterval = 100ms;
constexpr size_t maxResultsPerDisplay = 10'000;

// "1 item", "2 items"
[[nodiscard]] QString itemCountText(const uint64_t count)
{
	return count == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(count);
}

// The dialog palette's list viewer entries: normal, focused
TPalette& resultsPalette()
{
	static TPalette palette("\x1A\x1B", 2);
	return palette;
}

} // namespace

// The results' column titles, above them
class CTuiFindResultsHeader final : public TView
{
public:
	explicit CTuiFindResultsHeader(const TRect& bounds) noexcept : TView(bounds) { growMode = gfGrowHiX; }

	void draw() override
	{
		TDrawBuffer line;
		drawFileListHeader(line, size.x, FileListSortField::Name, Qt::AscendingOrder, FileTimeTextLength::Long, mapColor(1), mapColor(1));
		writeLine(0, 0, (short)size.x, 1, line);
	}

	TPalette& getPalette() const override { return resultsPalette(); }
};

// The results, sorted by name as they arrive. A link mark starts the row of an item found through a directory link.
// The cursor stays on the first row until the user moves it, then on its item.
class CTuiFindResultsList final : public CTuiRowList, private SortedFileListObserver
{
public:
	CTuiFindResultsList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept : CTuiRowList(bounds, verticalScrollBar) {}

	// Called when the item under the cursor may have changed: the cursor moved, or the items did
	std::function<void()> onCurrentItemChanged;

	// listedFolder: see fileListFolderPrefix()
	void clear(QString listedFolder)
	{
		_hashesReachedThroughLink.clear();
		_cursorFollowsItem = false;
		_list.setRows({}, std::move(listedFolder));
	}

	// Requires items not listed yet
	void add(std::vector<CFileSystemObject> items) { _list.addRows(std::move(items)); }
	void markReachedThroughLink(qulonglong itemHash) { _hashesReachedThroughLink.insert(itemHash); }

	void handleEvent(TEvent& event) override
	{
		CTuiRowList::handleEvent(event);
		clearPlainTextKey(event);
	}

	[[nodiscard]] const CFileSystemObject* currentItem() const { return _list.rowCount() > 0 ? &_list.rowAt(cursorRow()) : nullptr; }
	[[nodiscard]] int itemCount() const noexcept { return _list.rowCount(); }

	[[nodiscard]] QStringList paths() const
	{
		QStringList paths;
		paths.reserve(_list.rowCount());
		for (int row = 0, numRows = _list.rowCount(); row < numRows; ++row)
			paths.push_back(_list.rowAt(row).fullAbsolutePath());
		return paths;
	}

	void draw() override
	{
		const TColorAttr normalColor = mapColor(1), cursorColor = mapColor(2);
		const std::vector<FileListColumn> columns = fileListColumns(size.x, FileListSortField::Name, FileTimeTextLength::Long);
		const bool extensionShown = std::ranges::any_of(columns, [](const FileListColumn& column) { return column.field == FileListSortField::Extension; });
		for (int y = 0; y < size.y; ++y)
		{
			const int row = topRow() + y;
			const TColorAttr color = cursorIsShownOn(row) ? cursorColor : normalColor;

			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			if (row < _list.rowCount())
			{
				const CFileSystemObject& item = _list.rowAt(row);
				if (_hashesReachedThroughLink.contains(item.hash()))
					line.moveStr(0, "↪", color);
				for (const FileListColumn& column : columns)
				{
					QString text = fileListCellText(item, column.field, extensionShown, _list.listedFolder(), FileTimeTextLength::Long);
					if (column.field == FileListSortField::Name)
						text = elidedInTheMiddle(text, column.width);
					drawFileListCell(line, column, text, color);
				}
			}
			drawFileListColumnSeparators(line, columns, color);
			writeLine(0, (short)y, (short)size.x, 1, line);
		}
	}

	TPalette& getPalette() const override { return resultsPalette(); }

private:
	[[nodiscard]] int rowCount() const noexcept override { return _list.rowCount(); }

	void cursorMovedByUser() override
	{
		_cursorFollowsItem = true;
		onCurrentItemChanged();
	}

	void beforeChange() { _cursorItemHash = _cursorFollowsItem && currentItem() ? currentItem()->hash() : 0; }

	void afterChange()
	{
		const int itemRow = _cursorFollowsItem ? _list.rowOf(_cursorItemHash) : 0;
		rowsChanged(itemRow >= 0 ? itemRow : cursorRow());
		onCurrentItemChanged();
	}

	void beforeReset() override { beforeChange(); }
	void afterReset() override { afterChange(); }
	void beforeInsert(int, int) override { beforeChange(); }
	void afterInsert() override { afterChange(); }
	void beforeRemove(int, int) override { beforeChange(); }
	void afterRemove() override { afterChange(); }
	void beforeMove(int, int) override { beforeChange(); }
	void afterMove() override { afterChange(); }
	void rowChanged(int) override { drawView(); }
	void beforeReorder(SortedFileListReorderCause) override { beforeChange(); }
	void afterReorder(SortedFileListReorderCause) override { afterChange(); }

private:
	SortedFileList _list{ *this };
	ankerl::unordered_dense::set<qulonglong, identity_hash> _hashesReachedThroughLink;
	bool _cursorFollowsItem = false;
	qulonglong _cursorItemHash = 0; // From beforeChange() to afterChange()
};

CTuiFindDialog::CTuiFindDialog(const TRect& bounds) :
	TWindowInit(&TDialog::initFrame),
	TDialog(bounds, "Find files"),
	_nameHistory(KEY_FILE_SEARCH_NAME_HISTORY, findNameHistoryId),
	_contentsHistory(KEY_FILE_SEARCH_CONTENTS_HISTORY, findContentsHistoryId),
	_searchInHistory(KEY_FILE_SEARCH_ROOT_HISTORY, findSearchInHistoryId)
{
	flags &= ~wfMove;
	growMode = gfGrowHiX | gfGrowHiY;

	static constexpr int labelX = 2, fieldX = 20, listTop = 11, searchButtonWidth = 12, sendButtonWidth = 17;
	const int right = size.x - 2, statusY = size.y - 2, currentPathY = statusY - 1;

	const auto addField = [this](int y, int fieldEnd, const char* label, const CTuiSavedFieldHistory& history) {
		return insertFieldWithHistory(*this, history, label, y, labelX, fieldX, fieldEnd, inputLimit, true);
	};

	_name = addField(2, right, "Search ~f~or:", _nameHistory);
	_nameOptions = new TCheckBoxes({ fieldX, 3, right, 4 }, new TSItem("~P~artial match", new TSItem("Case sensi~t~ive", nullptr)));
	_nameOptions->growMode = gfGrowHiX;
	insert(_nameOptions);

	_contents = addField(5, right, "With c~o~ntents:", _contentsHistory); // Alt+C copies a result's path, as in the panel
	_contentsOptions = new TCheckBoxes({ fieldX, 6, right, 7 }, new TSItem("~W~hole words", new TSItem("~R~egex", new TSItem("Case sensiti~v~e", nullptr))));
	_contentsOptions->growMode = gfGrowHiX;
	insert(_contentsOptions);

	const int sendButtonX = right - sendButtonWidth, searchButtonX = sendButtonX - searchButtonWidth;
	_searchIn = addField(8, searchButtonX - 1, "Search ~i~n:", _searchInHistory);
	_excludedFolders = new TInputLine({ fieldX, 9, searchButtonX - 1, 10 }, inputLimit);
	_excludedFolders->growMode = gfGrowHiX;
	insert(_excludedFolders);
	insert(new TLabel({ labelX, 9, fieldX - 1, 10 }, "~E~xclude folders:", _excludedFolders));
	_searchButton = new TButton({ searchButtonX, 8, sendButtonX, 10 }, "Search", cmStartOrStopSearch, bfDefault);
	_searchButton->growMode = gfGrowLoX | gfGrowHiX;
	insert(_searchButton);
	auto* sendButton = new TButton({ sendButtonX, 8, right, 10 }, "Send to pa~n~el", cmSendResultsToPanel, bfNormal);
	sendButton->growMode = gfGrowLoX | gfGrowHiX;
	insert(sendButton);

	insert(new CTuiFindResultsHeader({ labelX, listTop, right - 1, listTop + 1 }));
	auto* scrollBar = new TScrollBar({ right - 1, listTop + 1, right, currentPathY });
	scrollBar->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
	insert(scrollBar);
	_results = new CTuiFindResultsList({ labelX, listTop + 1, right - 1, currentPathY }, scrollBar);
	_results->onCurrentItemChanged = [this] { showCurrentResultPath(); };
	insert(_results);

	_currentResultPath = new CTuiTextView({ labelX, currentPathY, right, currentPathY + 1 }, {});
	_currentResultPath->growMode = gfGrowLoY | gfGrowHiY | gfGrowHiX;
	insert(_currentResultPath);
	_status = new CTuiTextView({ labelX, statusY, right, statusY + 1 }, {});
	_status->growMode = gfGrowLoY | gfGrowHiY | gfGrowHiX;
	insert(_status);

	// The contents field starts empty: a pattern left in it would silently restrict the search
	const QSettings settings;
	setInputText(_name, _nameHistory.mostRecent());
	setInputText(_excludedFolders, settings.value(KEY_FILE_SEARCH_EXCLUDED_FOLDERS).toString());
	setChecked(_nameOptions, { settings.value(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, FILE_SEARCH_NAME_PARTIAL_MATCH_DEFAULT).toBool(),
		settings.value(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, false).toBool() });
	setChecked(_contentsOptions, { settings.value(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, false).toBool(),
		settings.value(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, false).toBool(), settings.value(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, false).toBool() });
}

CTuiFindDialog::Outcome CTuiFindDialog::exec(const QString& searchInText, const QString& baseDir)
{
	if (const TRect desktopArea = TProgram::deskTop->getExtent(); getBounds() != desktopArea)
	{
		changeBounds(desktopArea);
		showCurrentResultPath(); // Elided to the new width
	}

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
	if (auto* send = std::get_if<SendResultsToPanel>(&_outcome))
		send->paths = _results->paths();
	return std::exchange(_outcome, Outcome{});
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
			if (const CFileSystemObject* item = _results->currentItem(); item && item->isFile())
				key == kbF3 ? viewFile(item->fullAbsolutePath()) : editFile(item->fullAbsolutePath());
			clearEvent(event);
			return;
		}
		if (key == kbAltC)
		{
			if (const CFileSystemObject* item = _results->currentItem())
				CTuiApplication::copyToClipboard(shellQuotedPath(toNativeSeparators(item->fullAbsolutePath())));
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
	case cmSendResultsToPanel:
		sendResultsToPanel();
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
		.excludedFolders = excludedFolderFiltersFromSearchField(inputText(_excludedFolders)),
		.nameFilters = nameFiltersFromSearchField(nameText, isChecked(_nameOptions, PartialMatch)),
		.nameCaseSensitive = isChecked(_nameOptions, NameCaseSensitive),
		.contents = contentsText,
		.contentsCaseSensitive = isChecked(_contentsOptions, ContentsCaseSensitive),
		.contentsWholeWords = isChecked(_contentsOptions, WholeWords),
		.contentsIsRegex = isChecked(_contentsOptions, ContentsIsRegex)
	};

	if (!_engine.search(query, &_pendingResults))
	{
		setStatus(QStringLiteral("Nothing to search in"));
		return;
	}

	_nameHistory.record(nameText);
	_contentsHistory.record(contentsText);
	if (searchInFieldTextBelongsInHistory(searchInText))
		_searchInHistory.record(searchInText);

	_results->clear(deepestFolderHolding(query.roots));
	setStatus({});
	setSearchActive(true);
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + resultsDisplayInterval;
}

void CTuiFindDialog::showPendingResults(const size_t maxMatches)
{
	CFileSearchResultsBuffer::Batch batch = _pendingResults.take(maxMatches);

	if (!batch.lastScannedItem.isEmpty())
		setStatus(toNativeSeparators(batch.lastScannedItem));

	std::vector<CFileSystemObject> items;
	items.reserve(batch.matches.size());
	for (CFileSearchResultsBuffer::Match& match : batch.matches)
	{
		if (match.reachedThroughLink)
			_results->markReachedThroughLink(match.item.hash());
		items.push_back(std::move(match.item));
	}
	_results->add(std::move(items));

	// A full batch leaves more waiting
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + (batch.matches.size() < maxMatches ? resultsDisplayInterval : 0ms);

	if (batch.completion)
		showCompletion(*batch.completion);
}

void CTuiFindDialog::showCompletion(const CFileSearchResultsBuffer::Completion& completion)
{
	setSearchActive(false);

	if (completion.status == CFileSearchEngine::SearchInvalidPattern)
	{
		setStatus(QStringLiteral("The contents pattern is not a valid regular expression"));
		return;
	}

	QString message = completion.status == CFileSearchEngine::SearchCancelled ? QStringLiteral("Search aborted") : QStringLiteral("Search completed");
	message += QStringLiteral(", %1 found").arg(itemCountText((uint64_t)_results->itemCount()));
	// Neither matches nor known non-matches, so saying nothing would present the results as complete
	if (completion.inconclusiveItems > 0)
		message += QStringLiteral(", %1 could not be searched").arg(itemCountText(completion.inconclusiveItems));
	message += QStringLiteral(": %1 scanned in %2 sec").arg(itemCountText(completion.itemsScanned)).arg((double)completion.msElapsed * 1e-3, 0, 'f', 1);
	setStatus(message);

	// Unless the user is already going through them
	if (_results->itemCount() > 0 && !(_results->state & sfFocused))
		_results->select();
}

void CTuiFindDialog::setSearchActive(const bool active)
{
	_searchActive = active;
	delete[] const_cast<char*>(_searchButton->title);
	_searchButton->title = newStr(active ? "Stop" : "Search"); // No hot key: Enter in a field presses it, and Esc stops a search
	_searchButton->drawView();
}

void CTuiFindDialog::setStatus(const QString& text)
{
	_status->setLines({ elidedInTheMiddle(text, _status->size.x) });
}

void CTuiFindDialog::showCurrentResultPath()
{
	const CFileSystemObject* item = _results->currentItem();
	_currentResultPath->setLines({ item ? elidedInTheMiddle(toNativeSeparators(item->fullAbsolutePath()), _currentResultPath->size.x) : QString{} });
}

void CTuiFindDialog::goToCurrentResult()
{
	const CFileSystemObject* item = _results->currentItem();
	if (!item)
		return;

	_outcome = GoToResult{ item->fullAbsolutePath() };
	endModal(cmGoToResult);
}

void CTuiFindDialog::sendResultsToPanel()
{
	if (_results->itemCount() == 0)
		return;

	// exec() takes the paths once the search has stopped and every result is shown
	_outcome = SendResultsToPanel{};
	endModal(cmSendResultsToPanel);
}

void CTuiFindDialog::saveOptions() const
{
	QSettings settings;
	settings.setValue(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, isChecked(_nameOptions, PartialMatch));
	settings.setValue(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, isChecked(_nameOptions, NameCaseSensitive));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, isChecked(_contentsOptions, WholeWords));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, isChecked(_contentsOptions, ContentsIsRegex));
	settings.setValue(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, isChecked(_contentsOptions, ContentsCaseSensitive));
	settings.setValue(KEY_FILE_SEARCH_EXCLUDED_FOLDERS, inputText(_excludedFolders));
}
