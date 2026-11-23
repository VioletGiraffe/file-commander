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

#include "assert/advanced_assert.h"
#include "hash/hash_functors.hpp"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TScrollBar
#define Uses_TSItem
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <3rdparty/ankerl/unordered_dense.h>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <optional>
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

constexpr int labelX = 2, fieldX = 20;
// Below this width the options start under the labels, and the buttons move to the row below the fields
constexpr int sideBySideMinWidth = 69;
// Fits the contents options, the widest row
constexpr int minDialogWidth = 51;

[[nodiscard]] constexpr int resultsHeaderRow(const int dialogWidth) noexcept
{
	return dialogWidth >= sideBySideMinWidth ? 12 : 13;
}

// The desktop, or the minimum size that keeps every control whole: a smaller desktop clips the dialog
[[nodiscard]] TRect findDialogBounds(const TPoint desktopSize)
{
	static constexpr int minResultRows = 3, rowsBelowResults = 3; // The current path, the status and the frame
	const int width = std::max(desktopSize.x, minDialogWidth);
	const int height = std::max(desktopSize.y, resultsHeaderRow(width) + 1 + minResultRows + rowsBelowResults);
	return { 0, 0, width, height };
}

// The dialog palette's list viewer entries
constexpr uchar resultsNormalEntry = 0x1A, resultsFocusedEntry = 0x1B;

// The panel's sort keys: the menus that bind them receive no keys under a modal view
[[nodiscard]] std::optional<FileListSortField> sortFieldForKey(const ushort key)
{
	switch (key)
	{
	case kbCtrlF3: return FileListSortField::Name;
	case kbCtrlF4: return FileListSortField::Extension;
	case kbCtrlF5: return FileListSortField::Date;
	case kbCtrlF6: return FileListSortField::Size;
	default: return std::nullopt;
	}
}

} // namespace

// The results, in arrival order or sorted. A link mark starts the row of an item found through a directory link.
// The cursor stays on the newest row in arrival order, and on the first row of a sorted list, until the user moves it.
// A moved cursor stays on its item. In arrival order, a key or the wheel that reaches the last row makes it follow the newest row again.
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
	void markReachedThroughLink(const std::vector<uint64_t>& itemHashes) { _hashesReachedThroughLink.insert(itemHashes.begin(), itemHashes.end()); }

	// Takes the column from ascending to descending to arrival order. Returns the new order: empty for arrival order.
	std::optional<Qt::SortOrder> sortBy(const FileListSortField field)
	{
		const bool sortedByField = !_list.isInArrivalOrder() && _list.sortField() == field;
		if (sortedByField && _list.sortOrder() == Qt::DescendingOrder)
		{
			_list.showInArrivalOrder();
			return std::nullopt;
		}

		const Qt::SortOrder order = sortedByField ? Qt::DescendingOrder : Qt::AscendingOrder;
		_list.setSorting(field, order);
		return order;
	}

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
		const TColorAttr normalColor = mapColor(resultsNormalEntry), cursorColor = mapColor(resultsFocusedEntry);
		const std::vector<FileListColumn> columns = fileListColumns(size.x, _list.sortField(), FileTimeTextLength::Long);
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

private:
	[[nodiscard]] int rowCount() const noexcept override { return _list.rowCount(); }

	void cursorMovedByUser() override
	{
		_cursorFollowsItem = !_list.isInArrivalOrder() || cursorRow() < _list.rowCount() - 1;
		onCurrentItemChanged();
	}

	void rowClicked() override { _cursorFollowsItem = true; }

	void beforeChange() { _cursorItemHash = _cursorFollowsItem ? currentItem()->hash() : 0; }

	[[nodiscard]] int cursorRowAfterChange() const
	{
		if (!_cursorFollowsItem)
			return _list.isInArrivalOrder() ? _list.rowCount() - 1 : 0;

		// Only clear() removes rows, and it stops the cursor following its item
		const int itemRow = _list.rowOf(_cursorItemHash);
		assert_r(itemRow >= 0);
		return itemRow;
	}

	void afterChange()
	{
		const int row = cursorRowAfterChange();
		if (_cursorFollowsItem)
			rowsChangedKeepingCursorItem(row);
		else
			rowsChanged(row);
		onCurrentItemChanged();
	}

	void afterAddition()
	{
		rowsAdded(cursorRowAfterChange());
		onCurrentItemChanged();
	}

	void beforeReset() override { beforeChange(); }
	void afterReset() override { afterChange(); }
	void beforeInsert(int, int) override { beforeChange(); }
	void afterInsert() override { afterAddition(); }
	void beforeRemove(int, int) override { beforeChange(); }
	void afterRemove() override { afterChange(); }
	void beforeMove(int, int) override { beforeChange(); }
	void afterMove() override { afterChange(); }
	void rowChanged(int) override { drawView(); }
	void beforeReorder(SortedFileListReorderCause) override { beforeChange(); }
	void afterReorder(const SortedFileListReorderCause cause) override { cause == SortedFileListReorderCause::Addition ? afterAddition() : afterChange(); }

private:
	SortedFileList _list{ *this, SortedFileList::InitialOrder::Arrival };
	ankerl::unordered_dense::set<qulonglong, identity_hash> _hashesReachedThroughLink;
	bool _cursorFollowsItem = false;
	qulonglong _cursorItemHash = 0; // From beforeChange() to afterChange()
};

CTuiFindDialog::CTuiFindDialog(const TPoint desktopSize) :
	TWindowInit(&TDialog::initFrame),
	TDialog(findDialogBounds(desktopSize), "Find files"),
	_nameHistory(KEY_FILE_SEARCH_NAME_HISTORY, findNameHistoryId),
	_contentsHistory(KEY_FILE_SEARCH_CONTENTS_HISTORY, findContentsHistoryId),
	_searchInHistory(KEY_FILE_SEARCH_ROOT_HISTORY, findSearchInHistoryId)
{
	flags &= ~wfMove;

	// layOutViews() sets the real bounds
	// Must be one column wide: TScrollBar takes its orientation from its initial width
	const TRect unplaced{ 0, 0, 1, 1 };
	const auto addField = [this](int y, const char* label, const CTuiSavedFieldHistory& history) {
		return insertFieldWithHistory(*this, history, label, y, labelX, fieldX, size.x - 2, inputLimit);
	};

	_name = addField(2, "Search ~f~or:", _nameHistory);
	_nameOptions = new TCheckBoxes(unplaced, new TSItem("~P~artial match", new TSItem("Case sensi~t~ive", nullptr)));
	insert(_nameOptions);
	// Starts at "Any type" each time: a type left selected would silently restrict the search
	_itemType = new TRadioButtons(unplaced, new TSItem("~A~ny type", new TSItem("Fi~l~es", new TSItem("Fol~d~ers", nullptr))));
	insert(_itemType);

	_contents = addField(6, "With c~o~ntents:", _contentsHistory); // Alt+C copies a result's path, as in the panel
	_contentsOptions = new TCheckBoxes(unplaced, new TSItem("~W~hole words", new TSItem("~R~egex", new TSItem("Case sensiti~v~e", nullptr))));
	insert(_contentsOptions);

	_searchIn = addField(9, "Search ~i~n:", _searchInHistory);
	_excludedFolders = new TInputLine(unplaced, inputLimit);
	insert(_excludedFolders);
	insert(new TLabel({ labelX, 10, fieldX - 1, 11 }, "~E~xclude folders:", _excludedFolders));
	_searchButton = new TButton(unplaced, "Search", cmStartOrStopSearch, bfDefault);
	insert(_searchButton);
	_sendButton = new TButton(unplaced, "Send to pa~n~el", cmSendResultsToPanel, bfNormal);
	insert(_sendButton);

	_resultsHeader = new CTuiColumnHeader(unplaced, { FileTimeTextLength::Long, resultsNormalEntry, resultsNormalEntry });
	_resultsHeader->clearSorting();
	_resultsHeader->onColumnClicked = [this](const FileListSortField field) { sortResultsBy(field); };
	insert(_resultsHeader);
	_resultsScrollBar = new TScrollBar(unplaced);
	insert(_resultsScrollBar);
	_results = new CTuiFindResultsList(unplaced, _resultsScrollBar);
	_results->onCurrentItemChanged = [this] { showCurrentResultPath(); };
	insert(_results);

	_currentResultPath = new CTuiTextView(unplaced, {});
	insert(_currentResultPath);
	_status = new CTuiTextView(unplaced, {});
	insert(_status);

	layOutViews();

	// The contents field starts empty: a pattern left in it would silently restrict the search
	const QSettings settings;
	setInputText(_name.input, _nameHistory.mostRecent());
	setInputText(_excludedFolders, settings.value(KEY_FILE_SEARCH_EXCLUDED_FOLDERS).toString());
	setChecked(_nameOptions, { settings.value(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, FILE_SEARCH_NAME_PARTIAL_MATCH_DEFAULT).toBool(),
		settings.value(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, false).toBool() });
	setChecked(_contentsOptions, { settings.value(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, false).toBool(),
		settings.value(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, false).toBool(), settings.value(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, false).toBool() });
}

void CTuiFindDialog::calcBounds(TRect& bounds, TPoint)
{
	bounds = findDialogBounds(owner->size);
}

void CTuiFindDialog::changeBounds(const TRect& bounds)
{
	TDialog::changeBounds(bounds);
	layOutViews();
}

void CTuiFindDialog::layOutViews()
{
	static constexpr int searchButtonWidth = 12, sendButtonWidth = 17;
	const bool sideBySide = size.x >= sideBySideMinWidth;
	const int right = size.x - 2, statusY = size.y - 2, currentPathY = statusY - 1;
	const int sendButtonX = right - sendButtonWidth, searchButtonX = sendButtonX - searchButtonWidth;
	const int optionsX = sideBySide ? fieldX : labelX;
	const int pathFieldsEnd = sideBySide ? searchButtonX - 1 : right;
	const int buttonsY = sideBySide ? 9 : 11;
	const int headerY = resultsHeaderRow(size.x), listTop = headerY + 1;

	setFieldEnd(_name, right);
	_nameOptions->changeBounds({ optionsX, 3, right, 4 });
	_itemType->changeBounds({ optionsX, 4, right, 5 });
	setFieldEnd(_contents, right);
	_contentsOptions->changeBounds({ optionsX, 7, right, 8 });
	setFieldEnd(_searchIn, pathFieldsEnd);
	_excludedFolders->changeBounds({ fieldX, 10, pathFieldsEnd, 11 });
	_searchButton->changeBounds({ searchButtonX, buttonsY, sendButtonX, buttonsY + 2 });
	_sendButton->changeBounds({ sendButtonX, buttonsY, right, buttonsY + 2 });

	_resultsHeader->changeBounds({ labelX, headerY, right - 1, listTop });
	_resultsScrollBar->changeBounds({ right - 1, listTop, right, currentPathY });
	_results->changeBounds({ labelX, listTop, right - 1, currentPathY });
	_currentResultPath->changeBounds({ labelX, currentPathY, right, currentPathY + 1 });
	_status->changeBounds({ labelX, statusY, right, statusY + 1 });

	setStatus(_statusText);
	showCurrentResultPath();
	// Each view drew itself as it moved, but not where another one still covered it then
	redraw();
}

CTuiFindDialog::Outcome CTuiFindDialog::exec(const QString& searchInText, const QString& baseDir)
{
	if (const TRect fittedBounds = findDialogBounds(TProgram::deskTop->size); getBounds() != fittedBounds)
		changeBounds(fittedBounds);

	setInputText(_searchIn.input, searchInText);
	_baseDir = baseDir;
	_name.input->select();

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
	if (_searchActive && (event.what & (evKeyDown | evMouseDown | evMouseWheel)))
		_userActedDuringSearch = true;

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
		if (const std::optional<FileListSortField> field = sortFieldForKey(key))
		{
			sortResultsBy(*field);
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
	const QString nameText = inputText(_name.input), contentsText = inputText(_contents.input), searchInText = inputText(_searchIn.input);
	ushort itemType = 0;
	_itemType->getData(&itemType);
	const FileSearchQuery query{
		.roots = rootsFromSearchInField(searchInText, _baseDir),
		.excludedFolders = excludedFolderFiltersFromSearchField(inputText(_excludedFolders)),
		.nameFilters = nameFiltersFromSearchField(nameText, isChecked(_nameOptions, PartialMatch)),
		.nameCaseSensitive = isChecked(_nameOptions, NameCaseSensitive),
		.itemType = static_cast<FileSearchQuery::ItemType>(itemType),
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
	_userActedDuringSearch = false;
	setSearchActive(true);
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + resultsDisplayInterval;
}

void CTuiFindDialog::showPendingResults(const size_t maxMatches)
{
	CFileSearchResultsBuffer::Batch batch = _pendingResults.take(maxMatches);

	if (!batch.lastScannedItem.isEmpty())
		setStatus(toNativeSeparators(batch.lastScannedItem));

	const bool batchIsFull = batch.matches.size() == maxMatches;
	_results->markReachedThroughLink(batch.hashesReachedThroughLink);
	_results->add(std::move(batch.matches));

	// A full batch leaves more waiting
	_nextResultsDisplayTime = std::chrono::steady_clock::now() + (batchIsFull ? 0ms : resultsDisplayInterval);

	if (batch.completion)
		showCompletion(*batch.completion);
}

void CTuiFindDialog::showCompletion(const CFileSearchResultsBuffer::Completion& completion)
{
	setSearchActive(false);
	setStatus(fileSearchCompletionText(completion, (uint64_t)_results->itemCount()));

	if (_results->itemCount() > 0 && !_userActedDuringSearch)
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
	_statusText = text;
	_status->setLines({ elidedInTheMiddle(text, _status->size.x) });
}

void CTuiFindDialog::showCurrentResultPath()
{
	const CFileSystemObject* item = _results->currentItem();
	_currentResultPath->setLines({ item ? elidedInTheMiddle(toNativeSeparators(item->fullAbsolutePath()), _currentResultPath->size.x) : QString{} });
}

void CTuiFindDialog::sortResultsBy(const FileListSortField field)
{
	if (const std::optional<Qt::SortOrder> order = _results->sortBy(field))
		_resultsHeader->setSorting(field, *order);
	else
		_resultsHeader->clearSorting();
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
