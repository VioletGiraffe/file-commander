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
#include <QPalette>
#include <QShortcut>
#include <QStatusBar>
#include <QStyledItemDelegate>
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

class CCommentRowDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	// Zero width: a comment spans the row, so its length must not widen the first column
	[[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
	{
		QSize hint = QStyledItemDelegate::sizeHint(option, index);
		if (index.data(CCsvTableModel::CommentRowRole).toBool())
			hint.setWidth(0);
		return hint;
	}

protected:
	void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
	{
		QStyledItemDelegate::initStyleOption(option, index);
		if (!index.data(CCsvTableModel::CommentRowRole).toBool())
			return;

		option->font.setItalic(true);
		option->palette.setColor(QPalette::Text, option->palette.color(QPalette::PlaceholderText));
	}
};

} // namespace

CCsvViewerWindow::CCsvViewerWindow(QWidget* parent) noexcept :
	CPluginWindow(parent),
	_tableView(new QTableView(this)),
	_model(new CCsvTableModel(this))
{
	setCentralWidget(_tableView);
	_tableView->setModel(_model);
	_tableView->setItemDelegate(new CCommentRowDelegate(_tableView));
	// Spans keep their row numbers through a reset; every change that moves a comment row is a reset
	connect(_model, &QAbstractItemModel::modelReset, this, &CCsvViewerWindow::spanCommentRows);
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
	_commentLinesAction = viewMenu->addAction(tr("Show # lines as &comments"), this, [this] { reload(); });
	_commentLinesAction->setCheckable(true);

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

	return reload(CommentLines::Detect);
}

void CCsvViewerWindow::configureForQuickView()
{
	// The menu bar stays with this window, which is never shown, so the table offers the applicable actions instead.
	// None carries a shortcut, so none competes with the main window's.
	_tableView->addAction(_firstRowIsHeaderAction);
	_tableView->addAction(_commentLinesAction);
	_tableView->addAction(_delimiterMenuAction);
	_tableView->setContextMenuPolicy(Qt::ActionsContextMenu);
}

bool CCsvViewerWindow::reload(CommentLines commentLines)
{
	QFile file(_filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(dialogParent(), tr("Failed to read the file"), tr("Failed to load the file\n\n%1\n\n%2").arg(_filePath, file.errorString()));
		return false;
	}

	auto decoded = CTextEncodingDetector::decodeWithLocaleFallback(file.readAll());

	if (commentLines == CommentLines::Detect)
		_commentLinesAction->setChecked(csvHasCommentLines(decoded.text));
	const bool recognizeCommentLines = _commentLinesAction->isChecked();

	QChar delimiter;
	if (const QString chosenDelimiter = _delimiterGroup->checkedAction()->data().toString(); !chosenDelimiter.isEmpty())
		delimiter = chosenDelimiter.front();
	else if (_filePath.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive))
		delimiter = u'\t';
	else
		delimiter = detectCsvDelimiter(decoded.text, recognizeCommentLines);

	_model->setTable(parseCsv(std::move(decoded.text), delimiter, recognizeCommentLines));

	const bool firstRowIsHeader = _model->firstRowLooksLikeHeader();
	_model->setFirstRowIsHeader(firstRowIsHeader);
	_firstRowIsHeaderAction->setChecked(firstRowIsHeader);

	_tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder); // The reset dropped the sort; the header must not still claim one
	_tableView->resizeColumnsToContents(); // Samples resizeContentsPrecision() rows, not the whole table

	const CsvTable& table = _model->table();
	QString status = tr("%L1 rows, %L2 columns").arg(table.rowCount() - table.commentRowCount).arg(table.columnCount);
	if (table.commentRowCount > 0)
		status += tr(", %L1 comment lines").arg(table.commentRowCount);
	status += tr(" | %1 | delimiter: %2").arg(decoded.encoding, delimiterName(delimiter));
	statusBar()->showMessage(status);
	return true;
}

void CCsvViewerWindow::spanCommentRows()
{
	_tableView->clearSpans();

	const int columnCount = _model->columnCount();
	if (columnCount < 2) // Qt rejects a single-cell span
		return;

	for (int row = 0, rowCount = _model->rowCount(); row < rowCount; ++row)
	{
		if (_model->isCommentRow(row))
			_tableView->setSpan(row, 0, 1, columnCount);
	}
}

QWidget* CCsvViewerWindow::dialogParent() const
{
	return _tableView->window();
}
