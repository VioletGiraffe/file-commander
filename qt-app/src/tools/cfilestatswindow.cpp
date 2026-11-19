#include "cfilestatswindow.h"

#include "csortbydatatreeitem.h"

#include "ccontroller.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "qtcore_helpers/displayformatting.hpp"
#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "widgets/clabelelided.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/magic_enum/magic_enum.hpp>

#include <QDateTime>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <utility>

namespace columns {
	enum Columns {
		Name, Size, ModDate, Folder
	};
}

CFileStatsWindow::CFileStatsWindow(QWidget* parent, std::vector<QString> paths, CController& controller) :
	QMainWindow{ nullptr },
	_parentWindow{ parent },
	_noPathExistsMessage{ paths.size() == 1 ? tr("The path %1 doesn't exist.").arg(toNativeSeparators(paths.front())) : tr("None of the selected items exist.") },
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

	_list = new QTreeWidget;
	layout->addWidget(_list);

	auto* btnClose = new QPushButton(tr("Close"));
	layout->addWidget(btnClose);
	layout->setAlignment(btnClose, Qt::AlignRight);
	QObject::connect(btnClose, &QPushButton::clicked, this, &QMainWindow::close);

	_statusLabel = new CLabelElided{ tr("Scanning...") };
	_statusLabel->setPreferredWidthFollowsText(false); // The scanned folder's path must not widen the window
	_statusLabel->setStyleSheet("color: palette(placeholder-text);");
	layout->addWidget(_statusLabel);

	_list->setHeaderLabels({
		tr("Name"),
		tr("Size"),
		tr("Modification date"),
		tr("Folder")
	});
	_list->header()->setStretchLastSection(false);
	_list->setSortingEnabled(true);
	_list->sortByColumn(columns::Size, Qt::DescendingOrder);

	connect(_list, &QTreeWidget::itemActivated, this, [&controller, parent](QTreeWidgetItem* item) {
		const QString path = item->data(columns::Name, Qt::UserRole).toString();
		controller.goToItem(controller.activePanelPosition(), CFileSystemObject{ path });
		parent->activateWindow();
	});

	new QShortcut(QKeySequence{ "Esc" }, this, this, &QMainWindow::close);

	_progressTimer = new QTimer{ this };
	connect(_progressTimer, &QTimer::timeout, this, &CFileStatsWindow::showScanProgress);
	_progressTimer->start(100);
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

	fillFileList(stats);
}

void CFileStatsWindow::fillFileList(const FileStatistics& stats)
{
	_list->clear();

	QList<QTreeWidgetItem*> items;
	items.reserve(stats.largestFiles.size());

	for (const CFileSystemObject& file : stats.largestFiles)
	{
		auto* item = new CSortByDataTreeItem;
		item->setText(columns::Name, file.fullName().toString());
		item->setData(columns::Name, Qt::UserRole, file.fullAbsolutePath());

		item->setText(columns::Size, fileSizeToString(file.size()));
		item->setData(columns::Size, Qt::UserRole, (qulonglong)file.size());

		item->setText(columns::ModDate, fromTime_t(file.modificationTime()).toString("dd.MM.yyyy hh:mm:ss"));
		item->setData(columns::ModDate, Qt::UserRole, (qulonglong)file.modificationTime());

		item->setText(columns::Folder, file.parentDirPath());

		items.push_back(item);
	}

	_list->addTopLevelItems(items);

	for (int col = 0; col < _list->columnCount(); ++col)
		_list->resizeColumnToContents(col);
}
