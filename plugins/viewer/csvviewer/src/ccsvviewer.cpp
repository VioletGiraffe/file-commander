#include "ccsvviewer.h"

#include "ccsvcommentlistmodel.h"
#include "ccsvparser.h"
#include "ccsvtablemodel.h"


// Submodule includes
#include "ctextencodingdetector.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QActionGroup>
#include <QFile>
#include <QHeaderView>
#include <QListView>
#include <QMenu>
#include <QPalette>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
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
			return CCsvViewer::tr(option.name);
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

CCsvViewer::CCsvViewer() noexcept :
	_tableView(new QTableView(this)),
	_model(new CCsvTableModel(this)),
	_commentListModel(new CCsvCommentListModel(*_model, this)),
	_commentList(new QListView(this)),
	_statusBar(new QStatusBar(this))
{
	auto* splitter = new QSplitter(this);
	splitter->addWidget(_tableView);
	splitter->addWidget(_commentList);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 1);
	_statusBar->setSizeGripEnabled(false);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(splitter, 1);
	layout->addWidget(_statusBar);

	_tableView->setModel(_model);
	_tableView->setItemDelegate(new CCommentRowDelegate(_tableView));
	// Spans keep their row numbers through a reset; every change that moves a comment row is a reset
	connect(_model, &QAbstractItemModel::modelReset, this, &CCsvViewer::tableRowsChanged);
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

	auto* viewMenu = new QMenu(tr("&View"), this);
	_firstRowIsHeaderAction = viewMenu->addAction(tr("First row is &header"), this, [this](bool checked) { _model->setFirstRowIsHeader(checked); });
	_firstRowIsHeaderAction->setCheckable(true);
	_commentLinesAction = viewMenu->addAction(tr("Show # lines as &comments"), this, [this] { reload(); });
	_commentLinesAction->setCheckable(true);

	_commentList->setModel(_commentListModel);
	_commentList->setUniformItemSizes(true); // Millions of rows may be comments; measuring each one is not an option
	_commentList->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_commentList->hide();
	connect(_commentList, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) { goToRow(_commentListModel->tableRow(index.row())); });
	connect(_commentList, &QAbstractItemView::activated, this, [this](const QModelIndex& index) { goToRow(_commentListModel->tableRow(index.row())); });

	_commentListAction = viewMenu->addAction(tr("Comment &list"));
	_commentListAction->setCheckable(true);
	connect(_commentListAction, &QAction::toggled, _commentList, &QWidget::setVisible);
	connect(_commentListAction, &QAction::triggered, this, [this] { _commentListVisibilityChosen = true; }); // Not emitted for setChecked()

	viewMenu->addSeparator();
	_nextCommentAction = viewMenu->addAction(tr("&Next comment"), QKeySequence{ Qt::Key_F8 }, this, [this] { goToAdjacentComment(true); });
	_previousCommentAction = viewMenu->addAction(tr("&Previous comment"), QKeySequence{ Qt::SHIFT | Qt::Key_F8 }, this, [this] { goToAdjacentComment(false); });

	QMenu* delimiterMenu = viewMenu->addMenu(tr("&Delimiter"));
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
	addMenu(viewMenu);
}

QString CCsvViewer::tryLoadFile(const QString& path)
{
	_filePath = path;
	return reload(CommentLines::Detect);
}

QString CCsvViewer::reload(CommentLines commentLines)
{
	QFile file(_filePath);
	if (!file.open(QIODevice::ReadOnly))
		return tr("Failed to load the file\n\n%1\n\n%2").arg(_filePath, file.errorString());

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
	_statusBar->showMessage(status);
	return {};
}

void CCsvViewer::tableRowsChanged()
{
	_commentListModel->refresh();

	// Empty while the table is sorted, where there is nothing to navigate between
	const bool hasComments = _commentListModel->rowCount() > 0;

	if (!_commentListVisibilityChosen)
		_commentListAction->setChecked(hasComments);

	_commentList->setEnabled(hasComments);
	_nextCommentAction->setEnabled(hasComments);
	_previousCommentAction->setEnabled(hasComments);

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

void CCsvViewer::goToRow(int row)
{
	const QModelIndex target = _model->index(row, 0);
	_tableView->setCurrentIndex(target);
	_tableView->scrollTo(target, QAbstractItemView::PositionAtTop);
}

void CCsvViewer::goToAdjacentComment(bool forward)
{
	// Before the first row, so that the first comment is the next one from a table nothing is selected in
	const int currentRow = _tableView->currentIndex().isValid() ? _tableView->currentIndex().row() : -1;
	const int target = forward ? _commentListModel->commentRowAfter(currentRow) : _commentListModel->commentRowBefore(currentRow);
	if (target >= 0)
		goToRow(target);
}
