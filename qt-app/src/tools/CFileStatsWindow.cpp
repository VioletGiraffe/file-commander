#include "CFileStatsWindow.h"
#include "ccontroller.h"
#include "filesystemhelpers/filestatistics.h"
#include "filesystemhelperfunctions.h"
#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"

#include "3rdparty/magic_enum/magic_enum.hpp"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace columns {
	enum Columns {
		Name, Size, ModDate, Folder
	};
}

class SortByDataTreeItem final : public QTreeWidgetItem
{
public:
	using QTreeWidgetItem::QTreeWidgetItem;

	inline bool operator<(const QTreeWidgetItem& other) const override
	{
		const int col = treeWidget()->sortColumn();
		switch (col)
		{
		case columns::Name: [[fallthrough]];
		case columns::Folder:
			return CNaturalSorterQCollator{}.lessThan(text(col), other.text(col));
		case columns::Size: [[fallthrough]];
		case columns::ModDate:
			return data(col, Qt::UserRole).toULongLong() < other.data(col, Qt::UserRole).toULongLong();
		default: // Fallback
			return QTreeWidgetItem::operator<(other);
		}
	}
};

CFileStatsWindow::CFileStatsWindow(QWidget* parent,  const FileStatistics& stats, CController& controller) :
	QMainWindow{ nullptr }
{
	setWindowTitle(tr("Statistics"));

	const QString statsText = tr("Files: %1\nFolders: %2\nOccupied space: %3\n%4").
							  arg(stats.files).arg(stats.folders).arg(fileSizeToString(stats.occupiedSpace), fileSizeToString(stats.occupiedSpace, 'B', " "));

	setCentralWidget(new QWidget);
	auto* layout = new QVBoxLayout(centralWidget());

	layout->addWidget(new QLabel{ tr("Statistics for the selected items(including subitems):") });
	layout->addWidget(new QLabel{ statsText });

	layout->addWidget(new QLabel{ tr("Largest files (%1):").arg(stats.largestFiles.size()) });

	_list = new QTreeWidget;
	layout->addWidget(_list);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	layout->addWidget(buttons);

	QObject::connect(buttons, &QDialogButtonBox::clicked, this, &QMainWindow::close);

	_list->setHeaderLabels({
		tr("Name"),
		tr("Size"),
		tr("Modification date"),
		tr("Folder")
	});
	_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	_list->header()->setStretchLastSection(false);
	_list->setSortingEnabled(true);
	_list->sortByColumn(columns::Size, Qt::DescendingOrder);

	connect(_list, &QTreeWidget::itemActivated, this, [&controller, parent](QTreeWidgetItem* item) {
		const QString path = item->data(columns::Name, Qt::UserRole).toString();
		controller.activePanel().goToItem(CFileSystemObject{ path });
		parent->activateWindow();
	});

	fillFileList(stats);
}

void CFileStatsWindow::fillFileList(const FileStatistics& stats)
{
	_list->clear();

	QList<QTreeWidgetItem*> items;
	items.reserve(stats.largestFiles.size());

	for (const CFileSystemObject& file : stats.largestFiles)
	{
		auto* item = new SortByDataTreeItem;
		item->setText(columns::Name, file.fullName());
		item->setData(columns::Name, Qt::UserRole, file.fullAbsolutePath());

		item->setText(columns::Size, fileSizeToString(file.size()));
		item->setData(columns::Size, Qt::UserRole, file.size());

		item->setText(columns::ModDate, fromTime_t(file.modificationTime()).toString("dd.MM.yyyy hh:mm:ss"));
		item->setData(columns::ModDate, Qt::UserRole, (qulonglong)file.modificationTime());

		item->setText(columns::Folder, file.parentDirPath());

		items.push_back(item);
	}

	_list->addTopLevelItems(items);
}
