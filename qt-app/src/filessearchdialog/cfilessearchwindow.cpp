#include "cfilessearchwindow.h"

#include "cmainwindow.h"
#include "iconprovider/ciconprovider.h"

#include "ccontroller.h"
#include "cfilesystemobject.h"
#include "filesearchengine/filesearchquery.h"
#include "filesystemhelperfunctions.h"
#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QClipboard>
#include <QFileDialog>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <stddef.h>
#include <stdint.h>

// Results added to the list per event loop turn, so the window stays responsive while results arrive faster than it can add them
#ifdef __APPLE__
// genericIconFor fetches every row's icon from the system there: search results are rarely in its cache
static constexpr size_t maxRowsPerTurn = 10;
#else
static constexpr size_t maxRowsPerTurn = 200;
#endif

// Sorts by path, in the panel's natural order: the engine reports matches in no particular order
class CSearchResultItem final : public QListWidgetItem
{
public:
	bool operator<(const QListWidgetItem& other) const override
	{
		return NaturalSort::lessThan(data(Qt::UserRole).toString(), other.data(Qt::UserRole).toString());
	}
};

CFilesSearchWindow::CFilesSearchWindow(const QString& initialSearchInText, CIconProvider& iconProvider, QWidget* parent) :
	QMainWindow(parent),
	_iconProvider(iconProvider),
	// Queued also when requested on this thread, so the event loop runs between batches
	_pendingResults([this] { QMetaObject::invokeMethod(this, &CFilesSearchWindow::displayPendingResults, Qt::QueuedConnection); }),
	ui(new Ui::CFilesSearchWindow)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	enablePersistence(this, QSL("UI/FileSearchWindow"));

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

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

	connect(ui->nameToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->fileContentsToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);

	connect(ui->btnSaveResults, &QPushButton::clicked, this, &CFilesSearchWindow::saveResults);
	connect(ui->btnLoadResults, &QPushButton::clicked, this, &CFilesSearchWindow::loadResults);

	ui->resultsList->setSortingEnabled(true);
	connect(ui->resultsList, &QListWidget::itemActivated, [](QListWidgetItem* item) {
		CController& controller = CController::get();
		controller.goToItem(controller.activePanelPosition(), CFileSystemObject(item->data(Qt::UserRole).toString()));

		auto* mainWindow = CMainWindow::get();
		assert_and_return_r(mainWindow, );
		mainWindow->activateWindow();
	});
	connect(ui->resultsList, &QListWidget::customContextMenuRequested, this, &CFilesSearchWindow::showContextMenu);

	QMetaObject::invokeMethod(this, [this](){
		ui->nameToFind->setFocus();
		ui->nameToFind->lineEdit()->selectAll();
	}, Qt::QueuedConnection);
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	// The search threads post to this window through _pendingResults, so they must be gone before any of it is
	_engine.stopSearching();
	_engine.waitForSearchToFinish();

	QSettings s;
	s.setValue(KEY_FILE_SEARCH_NAME_CASE_SENSITIVE, ui->cbNameCaseSensitive->isChecked());
	s.setValue(KEY_FILE_SEARCH_NAME_PARTIAL_MATCH, ui->cbNamePartialMatch->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_CASE_SENSITIVE, ui->cbContentsCaseSensitive->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_WHOLE_WORDS, ui->cbContentsWholeWords->isChecked());
	s.setValue(KEY_FILE_SEARCH_CONTENTS_IS_REGEX, ui->cbRegexFileContents->isChecked());

	delete ui;
}

void CFilesSearchWindow::displayPendingResults()
{
	const CFileSearchResultsBuffer::Batch batch = _pendingResults.take(maxRowsPerTurn);

	if (!batch.lastScannedItem.isEmpty())
		setProgressText(batch.lastScannedItem, Qt::ElideMiddle);

	if (!batch.matches.empty())
	{
		ui->resultsList->setUpdatesEnabled(false);
		for (const CFileSearchResultsBuffer::Match& match : batch.matches)
		{
			_matches.push_back(match.item.fullAbsolutePath());
			addResultToUi(match.item, match.reachedThroughLink);
		}
		ui->resultsList->setUpdatesEnabled(true);
	}

	if (batch.completion)
		showSearchCompletion(*batch.completion);
}

void CFilesSearchWindow::showSearchCompletion(const CFileSearchResultsBuffer::Completion& completion)
{
	_searchActive = false;
	ui->btnSearch->setText(tr("Start"));

	if (completion.status == CFileSearchEngine::SearchInvalidPattern)
	{
		// No traversal happened, so the scan statistics below would all read zero
		setProgressText(tr("The contents pattern is not a valid regular expression"), Qt::ElideRight);
		return;
	}

	QString message = (completion.status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
	if (!_matches.empty())
		message = message % ", " % tr("%1 items found").arg(_matches.size());

	// Neither matches nor known non-matches, so saying nothing would present the results as complete
	if (completion.inconclusiveItems > 0)
		message = message % ", " % tr("%1 items could not be searched").arg(completion.inconclusiveItems);

	message = message % ": " % tr("%1 items scanned in %2 sec").arg(completion.itemsScanned).arg((double)completion.msElapsed * 1e-3, 0, 'f', 1);

	if (completion.msElapsed > 0)
	{
		const uint64_t itemsPerSecond = completion.itemsScanned * 1000ULL / completion.msElapsed;
		message = message % ", " % tr("search speed: %1 items/sec").arg(itemsPerSecond);
	}

	setProgressText(message, Qt::ElideRight);

	ui->resultsList->setFocus();
	if (ui->resultsList->count() > 0)
		ui->resultsList->item(0)->setSelected(true);
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

	_matches.clear();

	const QString filtersString = ui->nameToFind->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	const FileSearchQuery query{
		.roots = rootsFromSearchInField(ui->searchRoot->currentText(), CController::get().activePanel().currentDirPathPosix()),
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
		ui->searchRoot->addToHistory(ui->searchRoot->currentText());

		ui->btnSearch->setText(tr("Stop"));
		ui->resultsList->clear();

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

void CFilesSearchWindow::addResultToUi(const CFileSystemObject& object, bool reachedThroughLink)
{
	const QString& path = object.fullAbsolutePath();
	QString name = toNativeSeparators(path);
	if (object.isDir())
	{
		if (name.endsWith(nativeSeparator()))
			name.chop(1);
		name.prepend('[').append(']');
	}

	auto* item = new CSearchResultItem;
	item->setText(name);
	item->setIcon(_iconProvider.genericIconFor(object));
	item->setData(Qt::UserRole, path);

	if (reachedThroughLink)
	{
		// The same file can also be listed under its direct path; marking this one keeps the pair from reading as
		// a duplicate the search shouldn't have produced.
		item->setToolTip(tr("Found by following a directory link"));
		// The item's own font is still the default one until it joins the list, so italicize the list's font instead
		QFont font = ui->resultsList->font();
		font.setItalic(true);
		item->setFont(font);
	}

	ui->resultsList->addItem(item);
}

void CFilesSearchWindow::saveResults()
{
	if (_matches.empty())
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

	for (const auto& match : _matches)
	{
		file.write(match.toUtf8());
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

	ui->resultsList->setUpdatesEnabled(false);
	QString line;
	while (stream.readLineInto(&line))
	{
		_matches.push_back(line);
		addResultToUi(CFileSystemObject{ line }, false); // The saved file is a plain list of paths, with no record of how each was reached
	}
	ui->resultsList->setUpdatesEnabled(true);
}

void CFilesSearchWindow::showContextMenu(const QPoint& pos)
{
	QListWidgetItem *clickedItem = ui->resultsList->itemAt(pos);

	QMenu menu(this);
	const QAction *copyAction = menu.addAction("Copy path to clipboard");
	const QAction *selectedAction = menu.exec(ui->resultsList->mapToGlobal(pos));

	if (selectedAction == copyAction)
		QApplication::clipboard()->setText(shellQuotedPath(toNativeSeparators(clickedItem->data(Qt::UserRole).toString())));
}
