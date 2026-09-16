#include "ccsvviewerwindow.h"

#include "ccsvparser.h"
#include "ccsvtablemodel.h"


// Submodule includes
#include "ctextencodingdetector.h"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QActionGroup>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QStatusBar>
#include <QTableView>
RESTORE_COMPILER_WARNINGS

#include <utility>

namespace {

struct DelimiterOption {
	QChar character;
	const char* name;
};

constexpr DelimiterOption delimiterOptions[] = {
	{ u',', "Comma" },
	{ u';', "Semicolon" },
	{ u'\t', "Tab" },
	{ u'|', "Pipe" }
};

QString delimiterName(QChar delimiter)
{
	for (const DelimiterOption& option : delimiterOptions)
	{
		if (option.character == delimiter)
			return CCsvViewerWindow::tr(option.name);
	}

	return QString{ delimiter };
}

} // namespace

CCsvViewerWindow::CCsvViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent),
	_tableView(new QTableView(this)),
	_model(new CCsvTableModel(this))
{
	setCentralWidget(_tableView);
	_tableView->setModel(_model);
	_tableView->setWordWrap(false);
	_tableView->setAlternatingRowColors(true);
	_tableView->horizontalHeader()->setMaximumSectionSize(fontMetrics().averageCharWidth() * 100);
	// Fixed uniform height: measuring every one of millions of rows is not an option
	_tableView->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	_tableView->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 6);
	// The header's initial indicator is column 0: enabling sorting with it in place would sort the file on open
	_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
	_tableView->horizontalHeader()->setSortIndicatorClearable(true);
	_tableView->setSortingEnabled(true);

	QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
	fileMenu->addAction(tr("&Reload"), QKeySequence{ Qt::Key_F5 }, this, [this] { reload(); });
	fileMenu->addAction(tr("&Close"), this, &QWidget::close);

	QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
	_firstRowIsHeaderAction = viewMenu->addAction(tr("First row is &header"), this, [this](bool checked) { _model->setFirstRowIsHeader(checked); });
	_firstRowIsHeaderAction->setCheckable(true);

	QMenu* delimiterMenu = viewMenu->addMenu(tr("&Delimiter"));
	_delimiterMenuAction = delimiterMenu->menuAction();
	_delimiterGroup = new QActionGroup(this);
	const auto addDelimiterAction = [this, delimiterMenu](const QString& text, const QString& delimiter) {
		QAction* action = delimiterMenu->addAction(text);
		action->setCheckable(true);
		action->setData(delimiter);
		_delimiterGroup->addAction(action);
		return action;
	};
	addDelimiterAction(tr("&Auto-detect"), {})->setChecked(true);
	delimiterMenu->addSeparator();
	for (const DelimiterOption& option : delimiterOptions)
		addDelimiterAction(tr(option.name), QString{ option.character });
	connect(_delimiterGroup, &QActionGroup::triggered, this, [this] { reload(); });

	new QShortcut(QKeySequence{ QStringLiteral("Esc") }, this, this, &QWidget::close);

	enablePersistence(this, QStringLiteral("Plugins/CsvViewer/Window"), CPersistenceEnabler::Delayed{ false });
}

bool CCsvViewerWindow::loadFile(const QString& filePath)
{
	_filePath = filePath;

	const QFileInfo fi(filePath);
	setWindowTitle(fi.fileName() + " [" + fi.absolutePath() + "]");
	setWindowFilePath(filePath); // macOS only: the title bar's document proxy icon

	return reload();
}

void CCsvViewerWindow::configureForQuickView()
{
	// The menu bar stays with this window, which is never shown, so the table offers the applicable actions instead.
	// Neither carries a shortcut, so none competes with the main window's.
	_tableView->addAction(_firstRowIsHeaderAction);
	_tableView->addAction(_delimiterMenuAction);
	_tableView->setContextMenuPolicy(Qt::ActionsContextMenu);
}

bool CCsvViewerWindow::reload()
{
	QFile file(_filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(dialogParent(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\n%2").arg(_filePath, file.errorString()));
		return false;
	}

	auto decoded = CTextEncodingDetector::decodeWithLocaleFallback(file.readAll());

	QChar delimiter;
	if (const QString chosenDelimiter = _delimiterGroup->checkedAction()->data().toString(); !chosenDelimiter.isEmpty())
		delimiter = chosenDelimiter.front();
	else if (_filePath.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive))
		delimiter = u'\t';
	else
		delimiter = detectCsvDelimiter(decoded.text);

	_model->setTable(parseCsv(std::move(decoded.text), delimiter));

	const bool firstRowIsHeader = _model->firstRowLooksLikeHeader();
	_model->setFirstRowIsHeader(firstRowIsHeader);
	_firstRowIsHeaderAction->setChecked(firstRowIsHeader);

	_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder); // The reset dropped the sort; the header must not still claim one
	_tableView->resizeColumnsToContents(); // Samples resizeContentsPrecision() rows, not the whole table

	const CsvTable& table = _model->table();
	statusBar()->showMessage(tr("%L1 rows, %L2 columns | %3 | delimiter: %4").arg(table.rowCount()).arg(table.columnCount).arg(decoded.encoding, delimiterName(delimiter)));
	return true;
}

QWidget* CCsvViewerWindow::dialogParent() const
{
	return _tableView->window();
}
