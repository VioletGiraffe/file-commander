#include "cfilessearchwindow.h"
#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "../cmainwindow.h"
#include "settings/csettings.h"
#include "filesystemhelperfunctions.h"
#include "iconprovider/ciconprovider.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/cpersistentwindow.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilessearchwindow.h"

#include <QClipboard>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_NAME_TO_FIND            QSL("FileSearchDialog/Ui/NameToFind")
#define SETTINGS_NAME_CASE_SENSITIVE     QSL("FileSearchDialog/Ui/CaseSensitiveName")
#define SETTINGS_NAME_PARTIAL_MATCH      QSL("FileSearchDialog/Ui/NamePartialMatch")
#define SETTINGS_CONTENTS_TO_FIND        QSL("FileSearchDialog/Ui/ContentsToFind")
#define SETTINGS_CONTENTS_CASE_SENSITIVE QSL("FileSearchDialog/Ui/CaseSensitiveContents")
#define SETTINGS_CONTENTS_IS_REGEX       QSL("FileSearchDialog/Ui/ContentsIsRegex")
#define SETTINGS_ROOT_FOLDER             QSL("FileSearchDialog/Ui/RootFolder")

CFilesSearchWindow::CFilesSearchWindow(const std::vector<QString>& targets, QWidget* parent) :
	QMainWindow(parent),
	ui(new Ui::CFilesSearchWindow)
{
	ui->setupUi(this);

	setAttribute(Qt::WA_DeleteOnClose, true);

	installEventFilter(new CPersistenceEnabler(QSL("UI/FileSearchWindow"), this));

	connect(ui->btnSearch, &QPushButton::clicked, this, &CFilesSearchWindow::search);

	ui->nameToFind->setHistoryMode(true);
	ui->fileContentsToFind->setHistoryMode(true);
	ui->searchRoot->setHistoryMode(true);

	ui->nameToFind->enableAutoSave(SETTINGS_NAME_TO_FIND);
	ui->fileContentsToFind->enableAutoSave(SETTINGS_CONTENTS_TO_FIND);
	ui->fileContentsToFind->setSaveCurrentText(true);
	ui->searchRoot->enableAutoSave(SETTINGS_ROOT_FOLDER);

	QString pathsToSearchIn;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		pathsToSearchIn.append(targets[i]);
		if (i < targets.size() - 1)
			pathsToSearchIn.append(QSL("; "));
	}
	ui->searchRoot->setCurrentText(toNativeSeparators(pathsToSearchIn));

	CSettings s;
	ui->cbNameCaseSensitive->setChecked(s.value(SETTINGS_NAME_CASE_SENSITIVE, false).toBool());
	ui->cbNamePartialMatch->setChecked(s.value(SETTINGS_NAME_PARTIAL_MATCH, true).toBool());
	ui->cbContentsCaseSensitive->setChecked(s.value(SETTINGS_CONTENTS_CASE_SENSITIVE, false).toBool());
	ui->cbRegexFileContents->setChecked(s.value(SETTINGS_CONTENTS_IS_REGEX, false).toBool());

	connect(ui->nameToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);
	connect(ui->fileContentsToFind, &CHistoryComboBox::itemActivated, ui->btnSearch, &QPushButton::click);

	connect(ui->btnSaveResults, &QPushButton::clicked, this, &CFilesSearchWindow::saveResults);
	connect(ui->btnLoadResults, &QPushButton::clicked, this, &CFilesSearchWindow::loadResults);

	connect(ui->resultsList, &QListWidget::itemActivated, [](QListWidgetItem* item) {
		CController::get().activePanel().goToItem(CFileSystemObject(item->data(Qt::UserRole).toString()));
		CMainWindow::get()->activateWindow();
	});
	connect(ui->resultsList, &QListWidget::customContextMenuRequested, this, &CFilesSearchWindow::showContextMenu);

	QTimer::singleShot(0, this, [this](){
		ui->nameToFind->setFocus();
		ui->nameToFind->lineEdit()->selectAll();
	});
}

CFilesSearchWindow::~CFilesSearchWindow()
{
	_engine.stopSearching();

	CSettings s;
	s.setValue(SETTINGS_NAME_CASE_SENSITIVE, ui->cbNameCaseSensitive->isChecked());
	s.setValue(SETTINGS_NAME_PARTIAL_MATCH, ui->cbNamePartialMatch->isChecked());
	s.setValue(SETTINGS_CONTENTS_CASE_SENSITIVE, ui->cbContentsCaseSensitive->isChecked());
	s.setValue(SETTINGS_CONTENTS_IS_REGEX, ui->cbRegexFileContents->isChecked());

	delete ui;
}

void CFilesSearchWindow::itemScanned(const QString& currentItem)
{
	QMetaObject::invokeMethod(this, [=, this] {
			ui->progressLabel->setText(currentItem);
		},
		Qt::QueuedConnection
	);
}

void CFilesSearchWindow::matchFound(const QString& path)
{
	QMetaObject::invokeMethod(this, [=, this] {
			_matches.push_back(path);
			addResultToUi(path);
		},
		Qt::QueuedConnection
	);
}

void CFilesSearchWindow::searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t msElapsed)
{
	QMetaObject::invokeMethod(this, [=, this]{
			ui->btnSearch->setText(tr("Start"));
			QString message = (status == CFileSearchEngine::SearchCancelled ? tr("Search aborted") : tr("Search completed"));
			if (!_matches.empty())
				message = message % ", " % tr("%1 items found").arg(_matches.size());

			message = message % ": " % tr("%1 items scanned in %2 sec").arg(itemsScanned).arg((double)msElapsed * 1e-3, 0, 'f', 1);

			if (msElapsed > 0)
			{
				const uint64_t itemsPerSecond = itemsScanned * 1000ULL / msElapsed;
				message = message % ", " % tr("search speed: %1 items/sec").arg(itemsPerSecond);
			}

			ui->progressLabel->setText(message);

			ui->resultsList->setFocus();
			if (ui->resultsList->count() > 0)
				ui->resultsList->item(0)->setSelected(true);
		},
		Qt::QueuedConnection
	);
}

void CFilesSearchWindow::search()
{
	if (_engine.searchInProgress())
	{
		_engine.stopSearching();
		return;
	}

	_matches.clear();

	QString what = ui->nameToFind->currentText();
	const QString withText = ui->fileContentsToFind->currentText();

	if (ui->cbNamePartialMatch->isChecked())
	{
		if (!what.startsWith('*'))
			what.prepend('*');
		if (!what.endsWith('*'))
			what.append('*');
	}

	if (_engine.search(
		what,
		ui->cbNameCaseSensitive->isChecked(),
		ui->searchRoot->currentText().split(QSL("; ")),
		withText,
		ui->cbContentsCaseSensitive->isChecked(),
		ui->cbContentsWholeWords->isChecked(),
		ui->cbRegexFileContents->isChecked(),
		this /* listener */)
	)
	{
		ui->btnSearch->setText(tr("Stop"));
		ui->resultsList->clear();

		QString title = what;
		if (!withText.isEmpty())
			title += '/' + withText;
		title += ' ' + tr("search results");
		setWindowTitle(title);
	}
}

void CFilesSearchWindow::addResultToUi(const QString& path)
{
	ui->resultsList->setUpdatesEnabled(false);

	const CFileSystemObject object{ path };
	const bool isDir = object.isDir();

	QString name = toNativeSeparators(path);
	if (isDir)
	{
		if (name.endsWith(nativeSeparator()))
			name.chop(1);
		name.prepend('[').append(']');
	}

	auto* item = new QListWidgetItem;
	item->setText(name);
	item->setIcon(CIconProvider::iconForFilesystemObject(object, true));
	item->setData(Qt::UserRole, path);
	ui->resultsList->addItem(item);

	ui->resultsList->setUpdatesEnabled(true);
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

	QString line;
	while (stream.readLineInto(&line))
	{
		_matches.push_back(line);
		addResultToUi(line);
	}
}

void CFilesSearchWindow::showContextMenu(const QPoint& pos)
{
	QListWidgetItem *clickedItem = ui->resultsList->itemAt(pos);

	QMenu menu(this);
	const QAction *copyAction = menu.addAction("Copy path to clipboard");
	const QAction *selectedAction = menu.exec(ui->resultsList->mapToGlobal(pos));

	if (selectedAction == copyAction)
		QApplication::clipboard()->setText(escapedPath(toNativeSeparators(clickedItem->data(Qt::UserRole).toString())));
}
