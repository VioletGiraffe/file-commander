#include "cfilessearchwindow.h"

#include "cmainwindow.h"
#include "panel/columns.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "ccontroller.h"
#include "cfilesystemobject.h"
#include "filelist/filetimetext.h"
#include "filesearchengine/filesearchquery.h"
#include "filesystemhelperfunctions.h"
#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "hash/hash_functors.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <3rdparty/ankerl/unordered_dense.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStringList>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <stdint.h>
#include <utility>

// Each batch of results merges into the list once, a pass over all its rows: often enough to look immediate, rarely enough to stay cheap
static constexpr std::chrono::milliseconds resultsDisplayInterval{ 100 };

// "1 item", "2 items"
[[nodiscard]] static QString itemCountText(const uint64_t count)
{
	return count == 1 ? QObject::tr("1 item") : QObject::tr("%1 items").arg(count);
}

CFilesSearchWindow::CFilesSearchWindow(const QString& initialSearchInText, CIconProvider& iconProvider, QWidget* parent) :
	QMainWindow(parent),
	_iconProvider(iconProvider),
	ui(new Ui::CFilesSearchWindow)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	enablePersistence(this, QSL("UI/FileSearchWindow"));

	_results = new CFileListModel(&_iconProvider, { .dateLength = FileTimeTextLength::Long }, this);
	showFileListInTreeView(*_results, *ui->resultsList, NameColumn, Qt::AscendingOrder);
	_iconProvider.addIconsChangedListener(this);

	_resultsTimer = new QTimer(this);
	_resultsTimer->setInterval(resultsDisplayInterval);
	connect(_resultsTimer, &QTimer::timeout, this, &CFilesSearchWindow::displayPendingResults);

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	// Before the histories load: a combo box drops the items past its maximum
	for (CHistoryComboBox* field : { ui->nameToFind, ui->fileContentsToFind, ui->searchRoot })
		field->setMaxCount(FIELD_HISTORY_MAX_ENTRIES);
	ui->nameToFind->setSettingsKeyForHistoryPersistence(KEY_FILE_SEARCH_NAME_HISTORY);
	ui->fileContentsToFind->setSettingsKeyForHistoryPersistence(KEY_FILE_SEARCH_CONTENTS_HISTORY);
	ui->fileContentsToFind->lineEdit()->clear(); // A prefilled pattern would silently restrict the search to files containing it
	ui->searchRoot->setSettingsKeyForHistoryPersistence(KEY_FILE_SEARCH_ROOT_HISTORY);

	ui->searchRoot->setCurrentText(initialSearchInText);

	QSettings s;
	ui->cbNameCaseSensitive->setChecked(s.value(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, false).toBool());
	ui->cbNamePartialMatch->setChecked(s.value(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, FILE_SEARCH_NAME_PARTIAL_MATCH_DEFAULT).toBool());
	ui->cbContentsCaseSensitive->setChecked(s.value(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, false).toBool());
	ui->cbContentsWholeWords->setChecked(s.value(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, false).toBool());
	ui->cbRegexFileContents->setChecked(s.value(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, false).toBool());
	ui->excludedFolders->setText(s.value(KEY_FILE_SEARCH_EXCLUDED_FOLDERS).toString());

	connect(ui->nameToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->fileContentsToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->searchRoot, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->excludedFolders, &QLineEdit::returnPressed, ui->btnSearch, &QPushButton::click);

	connect(ui->btnSaveResults, &QPushButton::clicked, this, &CFilesSearchWindow::saveResults);
	connect(ui->btnLoadResults, &QPushButton::clicked, this, &CFilesSearchWindow::loadResults);
	connect(ui->btnSendToPanel, &QPushButton::clicked, this, &CFilesSearchWindow::sendResultsToPanel);

	connect(ui->resultsList, &QAbstractItemView::activated, this, [this](const QModelIndex& index) {
		CController& controller = CController::get();
		controller.goToItem(controller.activePanelPosition(), CFileSystemObject{ _results->rowAt(index).fullAbsolutePath() });

		auto* mainWindow = CMainWindow::get();
		assert_and_return_r(mainWindow, );
		mainWindow->activateWindow();
	});
	connect(ui->resultsList, &QWidget::customContextMenuRequested, this, &CFilesSearchWindow::showContextMenu);

	_copyPathAction = new QAction(tr("Copy path to clipboard"), this);
	_copyPathAction->setShortcut(QKeySequence{ QSL("Ctrl+Shift+X") }); // The main window's key for copying paths
	connect(_copyPathAction, &QAction::triggered, this, &CFilesSearchWindow::copyCurrentResultPath);
	addAction(_copyPathAction);

	QMetaObject::invokeMethod(this, [this](){
		ui->nameToFind->setFocus();
		ui->nameToFind->lineEdit()->selectAll();
	}, Qt::QueuedConnection);
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	// The search threads report into _pendingResults, so they must be gone before it is
	_engine.stopSearching();
	_engine.waitForSearchToFinish();
	_iconProvider.removeIconsChangedListener(this);

	QSettings s;
	s.setValue(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, ui->cbNameCaseSensitive->isChecked());
	s.setValue(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, ui->cbNamePartialMatch->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, ui->cbContentsCaseSensitive->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, ui->cbContentsWholeWords->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, ui->cbRegexFileContents->isChecked());
	s.setValue(KEY_FILE_SEARCH_EXCLUDED_FOLDERS, ui->excludedFolders->text());

	delete ui;
}

void CFilesSearchWindow::displayPendingResults()
{
	CFileSearchResultsBuffer::Batch batch = _pendingResults.take(SIZE_MAX);

	if (!batch.lastScannedItem.isEmpty())
		setProgressText(batch.lastScannedItem, Qt::ElideMiddle);

	std::vector<CFileSystemObject> rows;
	rows.reserve(batch.matches.size());
	for (CFileSearchResultsBuffer::Match& match : batch.matches)
	{
		// The same item can also be listed under its direct path: marking this one keeps the pair from reading as a duplicate
		if (match.reachedThroughLink)
			_results->markReachedThroughLink(match.item.hash());
		rows.push_back(std::move(match.item));
	}
	_results->addRows(std::move(rows));

	if (batch.completion)
		showSearchCompletion(*batch.completion);
}

void CFilesSearchWindow::showSearchCompletion(const CFileSearchResultsBuffer::Completion& completion)
{
	_searchActive = false;
	_resultsTimer->stop();
	ui->btnSearch->setText(tr("Start"));

	if (completion.status == CFileSearchEngine::SearchInvalidPattern)
	{
		// No traversal happened, so the scan statistics below would all read zero
		setProgressText(tr("The contents pattern is not a valid regular expression"), Qt::ElideRight);
		return;
	}

	QString message = (completion.status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
	if (const int found = _results->rowCount(); found > 0)
		message = message % ", " % tr("%1 found").arg(itemCountText((uint64_t)found));

	// Neither matches nor known non-matches, so saying nothing would present the results as complete
	if (completion.inconclusiveItems > 0)
		message = message % ", " % tr("%1 could not be searched").arg(itemCountText(completion.inconclusiveItems));

	message = message % ": " % tr("%1 scanned in %2 sec").arg(itemCountText(completion.itemsScanned)).arg((double)completion.msElapsed * 1e-3, 0, 'f', 1);

	if (completion.msElapsed > 0)
	{
		const uint64_t itemsPerSecond = completion.itemsScanned * 1000ULL / completion.msElapsed;
		message = message % ", " % tr("search speed: %1 items/sec").arg(itemsPerSecond);
	}

	setProgressText(message, Qt::ElideRight);

	ui->resultsList->setFocus();
	if (_results->rowCount() > 0)
		ui->resultsList->setCurrentIndex(_results->index(0, NameColumn));
}

// This label alternates between scanned paths and status sentences, which want different ends of the text kept
void CFilesSearchWindow::setProgressText(const QString& text, const Qt::TextElideMode elideMode)
{
	ui->progressLabel->setElideMode(elideMode);
	ui->progressLabel->setText(text);
}

void CFilesSearchWindow::search()
{
	if (_searchActive)
	{
		_engine.stopSearching();
		return;
	}

	const QString filtersString = ui->nameToFind->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	const FileSearchQuery query{
		.roots = rootsFromSearchInField(ui->searchRoot->currentText(), CController::get().activePanel().currentDirPathPosix()),
		.excludedFolders = excludedFolderFiltersFromSearchField(ui->excludedFolders->text()),
		.nameFilters = nameFiltersFromSearchField(filtersString, ui->cbNamePartialMatch->isChecked()),
		.nameCaseSensitive = ui->cbNameCaseSensitive->isChecked(),
		.contents = withText,
		.contentsCaseSensitive = ui->cbContentsCaseSensitive->isChecked(),
		.contentsWholeWords = ui->cbContentsWholeWords->isChecked(),
		.contentsIsRegex = ui->cbRegexFileContents->isChecked()
	};

	if (_engine.search(query, &_pendingResults))
	{
		_searchActive = true;
		ui->nameToFind->addToHistory(ui->nameToFind->currentText());
		ui->fileContentsToFind->addToHistory(ui->fileContentsToFind->currentText());
		if (searchInFieldTextBelongsInHistory(ui->searchRoot->currentText()))
			ui->searchRoot->addToHistory(ui->searchRoot->currentText());

		ui->btnSearch->setText(tr("Stop"));
		_results->setRows({}, deepestFolderHolding(query.roots));
		_resultsTimer->start();

		QString title = filtersString;
		if (!withText.isEmpty())
		{
			if (!title.isEmpty()) // The name field may be left empty to search by contents alone
				title += '/';
			title += withText;
		}
		title += ' ' + tr("search results");
		setWindowTitle(title);
	}
}

void CFilesSearchWindow::saveResults()
{
	if (_results->rowCount() == 0)
		return;

	const QString path = QFileDialog::getSaveFileName(this, {}, ui->searchRoot->currentText().split(';').front(), QSL("*.searchresult"));
	if (path.isEmpty())
		return;

	QFile file{ path };
	if (!file.open(QFile::WriteOnly))
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to save the search result to %1").arg(path));
		return;
	}

	for (const QString& resultPath : resultPaths())
	{
		file.write(resultPath.toUtf8());
		file.write("\n", 1);
	}
}

void CFilesSearchWindow::loadResults()
{
	const QString path = QFileDialog::getOpenFileName(this, {}, ui->searchRoot->currentText().split(';').front(), QSL("*.searchresult"));
	if (path.isEmpty())
		return;

	QFile file{ path };
	if (!file.open(QFile::ReadOnly))
	{
		QMessageBox::critical(this, tr("Error"), tr("Failed to open the file %1").arg(path));
		return;
	}

	QTextStream stream{ &file };
	stream.setEncoding(QStringConverter::Utf8);

	// The saved file is a plain list of paths, with no record of how each was reached.
	// A path listed already is skipped: the list holds each item once.
	ankerl::unordered_dense::set<qulonglong, identity_hash> loadedHashes;
	std::vector<CFileSystemObject> rows;
	QString line;
	while (stream.readLineInto(&line))
	{
		CFileSystemObject row{ line };
		if (!_results->indexByHash(row.hash()).isValid() && loadedHashes.insert(row.hash()).second)
			rows.push_back(std::move(row));
	}
	_results->addRows(std::move(rows));
}

void CFilesSearchWindow::sendResultsToPanel()
{
	if (_searchActive)
	{
		_engine.stopSearching();
		_engine.waitForSearchToFinish();
		// Shows everything the search reported: its completion clears _searchActive
		while (_searchActive)
			displayPendingResults();
	}

	if (_results->rowCount() == 0)
		return;

	CController& controller = CController::get();
	controller.showSearchResults(controller.activePanelPosition(), resultPaths());

	auto* mainWindow = CMainWindow::get();
	assert_and_return_r(mainWindow, );
	mainWindow->activateWindow();
}

QStringList CFilesSearchWindow::resultPaths() const
{
	QStringList paths;
	paths.reserve(_results->rowCount());
	for (int row = 0, numRows = _results->rowCount(); row < numRows; ++row)
		paths.push_back(_results->rowAt(row).fullAbsolutePath());
	return paths;
}

void CFilesSearchWindow::showContextMenu(const QPoint& pos)
{
	const QModelIndex clickedIndex = ui->resultsList->indexAt(pos);
	if (!clickedIndex.isValid())
		return;

	ui->resultsList->setCurrentIndex(clickedIndex); // The item the menu's action copies
	QMenu menu(this);
	menu.addAction(_copyPathAction);
	menu.exec(ui->resultsList->viewport()->mapToGlobal(pos));
}

void CFilesSearchWindow::copyCurrentResultPath() const
{
	if (const QModelIndex current = ui->resultsList->currentIndex(); current.isValid())
		QApplication::clipboard()->setText(shellQuotedPath(toNativeSeparators(_results->rowAt(current).fullAbsolutePath())));
}

void CFilesSearchWindow::onIconsChanged(const std::vector<qulonglong>& objectHashes)
{
	_results->onIconsChanged(objectHashes);
}

void CFilesSearchWindow::onAllIconsChanged()
{
	_results->onAllIconsChanged();
}
