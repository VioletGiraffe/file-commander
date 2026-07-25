#include "cfoldercomparisonwindow.h"
#include "csortbydatatreeitem.h"
#include "ccontroller.h"
#include "cpanel.h"
#include "filesystemhelperfunctions.h"

#include "dialogs/csimpleprogressdialog.h"

#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <atomic>
#include <thread>

namespace {

// Internal linkage: qt-app already has a "columns" namespace of its own for the file list.
namespace columns {
	enum Columns { Name, Status, LeftSize, RightSize };
}

QString statusText(const EntryDifference status)
{
	switch (status)
	{
	case EntryDifference::OnlyInLeft: return QObject::tr("Only on the left");
	case EntryDifference::OnlyInRight: return QObject::tr("Only on the right");
	case EntryDifference::Different: return QObject::tr("Different");
	case EntryDifference::ComparisonFailed: return QObject::tr("Could not be read");
	}

	return {};
}

// Directories carry no size worth showing, and neither does the side an entry is missing from.
QString sizeText(const FileSystemObjectType type, const uint64_t size)
{
	return type == File ? fileSizeToString(size) : QString{};
}

} // namespace

std::optional<FolderComparisonResult> runFolderComparison(QWidget* parent, const QString& leftRoot, const QString& rightRoot)
{
	CSimpleProgressDialog progressDialog{ parent };
	progressDialog.setWindowTitle(QObject::tr("Comparing folders"));
	progressDialog.setLabelText(QObject::tr("Scanning both folders..."));
	progressDialog.setCancellable(true);

	std::atomic<bool> abort{ false };
	QObject::connect(&progressDialog, &QDialog::rejected, [&abort]() { abort = true; });

	FolderComparisonResult result;
	std::thread worker{ [&]() {
		result = compareFolders(leftRoot, rightRoot, abort, [&progressDialog](const int percent) {
			QMetaObject::invokeMethod(&progressDialog, [&progressDialog, percent]() {
				progressDialog.setLabelText(QObject::tr("Comparing file contents..."));
				progressDialog.setValue(percent);
			}, Qt::QueuedConnection);
		});

		QMetaObject::invokeMethod(&progressDialog, [&progressDialog]() { progressDialog.accept(); }, Qt::QueuedConnection);
	} };

	progressDialog.exec();
	worker.join(); // The abort flag is polled per block, so cancelling returns promptly

	if (result.aborted)
		return {};

	return result;
}

CFolderComparisonWindow::CFolderComparisonWindow(QWidget* parent, const FolderComparisonResult& result, const QString& leftRoot,
	const QString& rightRoot, CController& controller) :
	QMainWindow{ nullptr }
{
	setWindowTitle(tr("Folder comparison"));

	setCentralWidget(new QWidget);
	auto* layout = new QVBoxLayout(centralWidget());

	layout->addWidget(new QLabel{ tr("Left: %1").arg(leftRoot) });
	layout->addWidget(new QLabel{ tr("Right: %1").arg(rightRoot) });

	const QString summary = result.differences.empty()
		? tr("The folders are identical: %1 files compared byte by byte.").arg(result.identicalFiles)
		: tr("Differences: %1. Identical files: %2.").arg(result.differences.size()).arg(result.identicalFiles);
	layout->addWidget(new QLabel{ summary });

	auto* list = new QTreeWidget;
	layout->addWidget(list);

	list->setHeaderLabels({ tr("Name"), tr("Status"), tr("Size on the left"), tr("Size on the right") });
	list->header()->setStretchLastSection(false);
	list->setRootIsDecorated(false);
	list->setSortingEnabled(true);
	list->sortByColumn(columns::Name, Qt::AscendingOrder);

	QList<QTreeWidgetItem*> items;
	items.reserve(static_cast<qsizetype>(result.differences.size()));

	for (const FolderComparisonEntry& entry : result.differences)
	{
		auto* item = new CSortByDataTreeItem;
		item->setText(columns::Name, entry.relativePath);
		item->setText(columns::Status, statusText(entry.status));

		item->setText(columns::LeftSize, sizeText(entry.leftType, entry.leftSize));
		item->setData(columns::LeftSize, Qt::UserRole, (qulonglong)entry.leftSize);
		item->setText(columns::RightSize, sizeText(entry.rightType, entry.rightSize));
		item->setData(columns::RightSize, Qt::UserRole, (qulonglong)entry.rightSize);

		// Activating an entry reveals it in whichever panel actually holds it.
		const bool onTheRightOnly = entry.status == EntryDifference::OnlyInRight;
		const QString absolutePath = QDir{ onTheRightOnly ? rightRoot : leftRoot }.absoluteFilePath(entry.relativePath);
		item->setData(columns::Name, Qt::UserRole, absolutePath);
		item->setData(columns::Name, Qt::UserRole + 1, static_cast<int>(onTheRightOnly ? Panel::RightPanel : Panel::LeftPanel));

		items.push_back(item);
	}

	list->addTopLevelItems(items);

	for (int col = 0; col < list->columnCount(); ++col)
		list->resizeColumnToContents(col);

	connect(list, &QTreeWidget::itemActivated, this, [&controller, parent](QTreeWidgetItem* item) {
		const QString path = item->data(columns::Name, Qt::UserRole).toString();
		const auto panel = static_cast<Panel>(item->data(columns::Name, Qt::UserRole + 1).toInt());
		controller.panel(panel).goToItem(CFileSystemObject{ path });
		parent->activateWindow();
	});

	auto* btnClose = new QPushButton(tr("Close"));
	layout->addWidget(btnClose);
	layout->setAlignment(btnClose, Qt::AlignRight);
	connect(btnClose, &QPushButton::clicked, this, &QMainWindow::close);

	new QShortcut(QKeySequence{ "Esc" }, this, this, &QMainWindow::close);
}
