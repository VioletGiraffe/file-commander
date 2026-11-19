#include "cfoldercomparisonwindow.h"

#include "csortbydatatreeitem.h"

#include "ccontroller.h"
#include "cpanel.h"
#include "filecomparator/cfoldercomparison.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/displayformatting.hpp"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <optional>

namespace {

// Internal linkage: qt-app already has a "columns" namespace of its own for the file list.
namespace columns {
	enum Columns { Name, Status, LeftSize, RightSize };
}

QString sizeText(const std::optional<uint64_t> size)
{
	return size ? fileSizeToString(*size) : QString{};
}

} // namespace

CFolderComparisonWindow::CFolderComparisonWindow(QWidget* parent, const CFolderComparison& comparison, CController& controller) :
	QMainWindow{ nullptr }
{
	assert_and_return_r(comparison.result(), );
	const FolderComparisonResult& result = *comparison.result();

	setWindowTitle(tr("Folder comparison"));

	setCentralWidget(new QWidget);
	auto* layout = new QVBoxLayout(centralWidget());

	layout->addWidget(new QLabel{ tr("Left: %1").arg(comparison.leftRoot()) });
	layout->addWidget(new QLabel{ tr("Right: %1").arg(comparison.rightRoot()) });
	layout->addWidget(new QLabel{ folderComparisonSummary(result) });

	auto* list = new QTreeWidget;
	list->setObjectName(QStringLiteral("differencesList")); // Named for CPersistenceEnabler
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
		item->setText(columns::Status, folderComparisonStatusText(entry.status));

		item->setText(columns::LeftSize, sizeText(entry.leftSize));
		item->setData(columns::LeftSize, Qt::UserRole, (qulonglong)entry.leftSize.value_or(0));
		item->setText(columns::RightSize, sizeText(entry.rightSize));
		item->setData(columns::RightSize, Qt::UserRole, (qulonglong)entry.rightSize.value_or(0));

		// Activating an entry reveals it in whichever panel actually holds it.
		const CFolderComparison::EntryLocation location = comparison.locationOf(entry);
		item->setData(columns::Name, Qt::UserRole, location.absolutePath);
		item->setData(columns::Name, Qt::UserRole + 1, static_cast<int>(location.panel));

		items.push_back(item);
	}

	list->addTopLevelItems(items);

	for (int col = 0; col < list->columnCount(); ++col)
		list->resizeColumnToContents(col);

	connect(list, &QTreeWidget::itemActivated, this, [&controller, parent](QTreeWidgetItem* item) {
		const QString path = item->data(columns::Name, Qt::UserRole).toString();
		const auto panel = static_cast<Panel>(item->data(columns::Name, Qt::UserRole + 1).toInt());
		controller.goToItem(panel, CFileSystemObject{ path });
		parent->activateWindow();
	});

	auto* btnClose = new QPushButton(tr("Close"));
	layout->addWidget(btnClose);
	layout->setAlignment(btnClose, Qt::AlignRight);
	connect(btnClose, &QPushButton::clicked, this, &QMainWindow::close);

	new QShortcut(QKeySequence{ "Esc" }, this, this, &QMainWindow::close);

	// No default size: the caller sizes the window for when nothing is stored
	enablePersistence(this, QStringLiteral("UI/FolderComparisonWindow"), CPersistenceEnabler::Delayed{ true }, CPersistenceEnabler::SetDefaultSize{ false });
}
