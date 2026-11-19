#include "cfilestatswindow.h"

#include "panel/columns.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "ccontroller.h"
#include "cfilesystemobject.h"
#include "filelist/filetimetext.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "qtcore_helpers/displayformatting.hpp"
#include "widgets/clabelelided.h"


DISABLE_COMPILER_WARNINGS
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStringList>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <utility>

CFileStatsWindow::CFileStatsWindow(QWidget* parent, std::vector<QString> paths, CController& controller, CIconProvider& iconProvider) :
	QMainWindow{ nullptr },
	_parentWindow{ parent },
	_iconProvider{ iconProvider },
	_noPathExistsMessage{ paths.size() == 1 ? tr("The path %1 doesn't exist.").arg(toNativeSeparators(paths.front())) : tr("None of the selected items exist.") },
	_listedFolder{ deepestFolderHolding(QStringList{ paths.cbegin(), paths.cend() }) },
	_scan{ std::move(paths) }
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Statistics"));

	setCentralWidget(new QWidget);
	auto* layout = new QVBoxLayout(centralWidget());

	layout->addWidget(new QLabel{ tr("Statistics for the selected items (including subitems):") });

	_summaryLabel = new QLabel;
	layout->addWidget(_summaryLabel);

	_largestFilesLabel = new QLabel;
	layout->addWidget(_largestFilesLabel);

	_list = new QTreeView;
	_list->setTextElideMode(Qt::ElideMiddle);
	layout->addWidget(_list);

	auto* btnClose = new QPushButton(tr("Close"));
	layout->addWidget(btnClose);
	layout->setAlignment(btnClose, Qt::AlignRight);
	QObject::connect(btnClose, &QPushButton::clicked, this, &QMainWindow::close);

	_statusLabel = new CLabelElided{ tr("Scanning...") };
	_statusLabel->setPreferredWidthFollowsText(false); // The scanned folder's path must not widen the window
	_statusLabel->setStyleSheet("color: palette(placeholder-text);");
	layout->addWidget(_statusLabel);

	_largestFiles = new CFileListModel(&_iconProvider, { .dateLength = FileTimeTextLength::Long, .sizeText = FileListCellFormat::SizeText::Precise }, this);
	showFileListInTreeView(*_largestFiles, *_list, SizeColumn, Qt::DescendingOrder);
	_iconProvider.addIconsChangedListener(this);

	connect(_list, &QAbstractItemView::activated, this, [this, &controller, parent](const QModelIndex& index) {
		controller.goToItem(controller.activePanelPosition(), CFileSystemObject{ _largestFiles->rowAt(index).fullAbsolutePath() });
		parent->activateWindow();
	});

	new QShortcut(QKeySequence{ "Esc" }, this, this, &QMainWindow::close);

	_progressTimer = new QTimer{ this };
	connect(_progressTimer, &QTimer::timeout, this, &CFileStatsWindow::showScanProgress);
	_progressTimer->start(100);
}

CFileStatsWindow::~CFileStatsWindow()
{
	_iconProvider.removeIconsChangedListener(this);
}

void CFileStatsWindow::showScanProgress()
{
	const bool finished = _scan.finished();
	if (finished)
	{
		_progressTimer->stop();
		// Empty when no selected path is an existing file or folder
		if (const auto& result = _scan.result(); result && result->stats.empty())
		{
			QMessageBox::warning(_parentWindow, tr("Statistics"), _noPathExistsMessage);
			close();
			return;
		}
	}

	// The first poll shows the window
	if (!isVisible())
		show();

	if (!finished)
	{
		if (const QString folder = _scan.currentFolder(); !folder.isEmpty())
			_statusLabel->setText(tr("Scanning %1...").arg(toNativeSeparators(folder)));
		return;
	}

	if (const auto& result = _scan.result())
		showResult(*result);
	else
		_statusLabel->setText(tr("The scan failed"));
}

void CFileStatsWindow::showResult(const CFileStatisticsScan::Result& result)
{
	const FileStatistics& stats = result.stats;
	_summaryLabel->setText(tr("Files: %1\nFolders: %2\nOccupied space: %3\n%4").arg(stats.files).arg(stats.folders)
		.arg(fileSizeToString(stats.occupiedSpace), fileSizeToString(stats.occupiedSpace, { .maxUnit = 'B', .thousandsSeparator = " " })));
	_largestFilesLabel->setText(tr("Largest files (%1):").arg(stats.largestFiles.size()));
	_statusLabel->setText(tr("Scanned in %1 s").arg(result.elapsedMs * 1e-3f, 0, 'f', 1));

	_largestFiles->setRows(stats.largestFiles, _listedFolder);
}

void CFileStatsWindow::onIconsChanged(const std::vector<qulonglong>& objectHashes)
{
	_largestFiles->onIconsChanged(objectHashes);
}

void CFileStatsWindow::onAllIconsChanged()
{
	_largestFiles->onAllIconsChanged();
}
