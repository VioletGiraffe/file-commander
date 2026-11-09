#include "cbatchrenamedialog.h"

#include "cmainwindow.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "fileoperations/fileoperationtexts.h"
#include "fileoperations/newnamecheck.h"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/chistorycombobox.h"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStringBuilder>
#include <QTableWidget>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <utility>
#include <vector>

#define SETTINGS_NAME_MASK             QSL("BatchRenameDialog/Ui/NameMask")
#define SETTINGS_EXTENSION_MASK        QSL("BatchRenameDialog/Ui/ExtensionMask")
#define SETTINGS_FIND                  QSL("BatchRenameDialog/Ui/Find")
#define SETTINGS_REPLACE_WITH          QSL("BatchRenameDialog/Ui/ReplaceWith")
#define SETTINGS_FIND_IS_REGEX         QSL("BatchRenameDialog/Ui/FindIsRegex")
#define SETTINGS_FIND_IS_CASE_SENSITIVE QSL("BatchRenameDialog/Ui/FindIsCaseSensitive")
#define SETTINGS_COUNTER_START         QSL("BatchRenameDialog/Ui/CounterStart")
#define SETTINGS_COUNTER_STEP          QSL("BatchRenameDialog/Ui/CounterStep")
#define SETTINGS_COUNTER_DIGITS        QSL("BatchRenameDialog/Ui/CounterDigits")

namespace columns {
	enum Columns {
		Name, NewName, Status, Count
	};
}

[[nodiscard]] static BatchRenameSource sourceOf(const CFileSystemObject& item, const CEntryPath& path)
{
	const CEntryPath parent = path.parent();
	return {
		.name = item.name().toString(),
		.extension = item.extension().toString(),
		.parentFolderName = parent.isRoot() ? QString{} : parent.name(),
		.modificationTime = QDateTime::fromSecsSinceEpoch(item.modificationTime()),
	};
}

CBatchRenameDialog::CBatchRenameDialog(QWidget* parent, const std::vector<CFileSystemObject>& items) :
	QDialog(parent)
{
	setWindowTitle(tr("Batch rename"));

	for (const CFileSystemObject& item : items)
	{
		// A real panel item always has a valid absolute path, and a root is never listed as an item
		const auto path = parseOperationPath(item.fullAbsolutePath());
		if (path && !path->isRoot())
			_rows.push_back({ .item = item, .path = *path });
	}

	_rulesPanel = new QWidget;
	_nameMask = new CHistoryComboBox(_rulesPanel);
	_extensionMask = new CHistoryComboBox(_rulesPanel);
	_find = new CHistoryComboBox(_rulesPanel);
	_replaceWith = new CHistoryComboBox(_rulesPanel);
	_findIsRegex = new QCheckBox(tr("Regular expression"));
	_findIsCaseSensitive = new QCheckBox(tr("Case-sensitive"));
	_findError = new QLabel;
	_findError->setStyleSheet(QSL("color: red;"));
	_findError->hide();

	// In LetterCase order
	_letterCase = new QComboBox;
	_letterCase->addItems({ tr("Unchanged"), tr("lowercase"), tr("UPPERCASE"), tr("Title Case") });

	_counterStart = new QSpinBox;
	_counterStart->setRange(0, 999'999'999);
	_counterStep = new QSpinBox;
	_counterStep->setRange(1, 1'000'000);
	_counterDigits = new QSpinBox;
	_counterDigits->setRange(1, 10);

	// The fields that change names on their own start at the default rules; the history keeps what was used before
	const BatchRenameRules defaults;
	_nameMask->setSettingsKeyForHistoryPersistence(SETTINGS_NAME_MASK);
	_extensionMask->setSettingsKeyForHistoryPersistence(SETTINGS_EXTENSION_MASK);
	_find->setSettingsKeyForHistoryPersistence(SETTINGS_FIND);
	_replaceWith->setSettingsKeyForHistoryPersistence(SETTINGS_REPLACE_WITH);
	_nameMask->setCurrentText(defaults.nameMask);
	_extensionMask->setCurrentText(defaults.extensionMask);
	_find->setCurrentText(defaults.find);
	_replaceWith->setCurrentText(defaults.replaceWith);

	const QSettings s;
	_findIsRegex->setChecked(s.value(SETTINGS_FIND_IS_REGEX, defaults.findIsRegex).toBool());
	_findIsCaseSensitive->setChecked(s.value(SETTINGS_FIND_IS_CASE_SENSITIVE, defaults.findIsCaseSensitive).toBool());
	_counterStart->setValue(s.value(SETTINGS_COUNTER_START, defaults.counterStart).toInt());
	_counterStep->setValue(s.value(SETTINGS_COUNTER_STEP, defaults.counterStep).toInt());
	_counterDigits->setValue(s.value(SETTINGS_COUNTER_DIGITS, defaults.counterDigits).toInt());

	auto* tokensHelp = new QLabel(tr("[N] name, [N2-5] its characters 2 to 5, [E] extension, [P] parent folder, [C] counter, "
		"[Y] [M] [D] [h] [m] [s] modification time"));
	tokensHelp->setWordWrap(true);
	tokensHelp->setStyleSheet(QSL("color: palette(placeholder-text);"));

	auto* replacement = new QHBoxLayout;
	replacement->addWidget(_find, 1);
	replacement->addWidget(new QLabel(tr("with")));
	replacement->addWidget(_replaceWith, 1);

	auto* findOptions = new QHBoxLayout;
	findOptions->addWidget(_findIsRegex);
	findOptions->addWidget(_findIsCaseSensitive);
	findOptions->addStretch();

	auto* counterOptions = new QHBoxLayout;
	counterOptions->addWidget(new QLabel(tr("Start:")));
	counterOptions->addWidget(_counterStart);
	counterOptions->addWidget(new QLabel(tr("Step:")));
	counterOptions->addWidget(_counterStep);
	counterOptions->addWidget(new QLabel(tr("Digits:")));
	counterOptions->addWidget(_counterDigits);
	counterOptions->addStretch();

	auto* rulesLayout = new QFormLayout(_rulesPanel);
	rulesLayout->setContentsMargins({});
	rulesLayout->addRow(tr("Name mask:"), _nameMask);
	rulesLayout->addRow(tr("Extension mask:"), _extensionMask);
	rulesLayout->addRow(QString{}, tokensHelp);
	rulesLayout->addRow(tr("Replace:"), replacement);
	rulesLayout->addRow(QString{}, findOptions);
	rulesLayout->addRow(QString{}, _findError);
	rulesLayout->addRow(tr("Letter case:"), _letterCase);
	rulesLayout->addRow(tr("Counter [C]:"), counterOptions);

	_preview = new QTableWidget(static_cast<int>(_rows.size()), columns::Count);
	_preview->setHorizontalHeaderLabels({ tr("Name"), tr("New name"), tr("Status") });
	_preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	_preview->horizontalHeader()->setSectionResizeMode(columns::Status, QHeaderView::ResizeToContents);
	_preview->verticalHeader()->hide();
	_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_preview->setSelectionMode(QAbstractItemView::NoSelection);
	_preview->setWordWrap(false);
	for (int row = 0; row < _preview->rowCount(); ++row)
	{
		for (int column = 0; column < columns::Count; ++column)
			_preview->setItem(row, column, new QTableWidgetItem);
	}

	_applyButton = new QPushButton(tr("Rename"));
	_undoButton = new QPushButton(tr("Undo"));
	_closeButton = new QPushButton(tr("Close"));
	auto* buttons = new QHBoxLayout;
	buttons->addStretch();
	for (QPushButton* button : { _applyButton, _undoButton, _closeButton })
	{
		button->setAutoDefault(false); // Enter in a field must not rename
		buttons->addWidget(button);
	}

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(_rulesPanel);
	layout->addWidget(_preview, 1);
	layout->addLayout(buttons);

	for (CHistoryComboBox* field : { _nameMask, _extensionMask, _find, _replaceWith })
		connect(field, &QComboBox::editTextChanged, this, &CBatchRenameDialog::rulesChanged);
	for (QCheckBox* option : { _findIsRegex, _findIsCaseSensitive })
		connect(option, &QCheckBox::toggled, this, &CBatchRenameDialog::rulesChanged);
	connect(_letterCase, &QComboBox::currentIndexChanged, this, &CBatchRenameDialog::rulesChanged);
	for (QSpinBox* option : { _counterStart, _counterStep, _counterDigits })
		connect(option, &QSpinBox::valueChanged, this, &CBatchRenameDialog::rulesChanged);

	connect(_applyButton, &QPushButton::clicked, this, &CBatchRenameDialog::applyRenaming);
	connect(_undoButton, &QPushButton::clicked, this, &CBatchRenameDialog::undoRenaming);
	connect(_closeButton, &QPushButton::clicked, this, &CBatchRenameDialog::reject);

	enablePersistence(this, QSL("UI/BatchRenameDialog"));
	updatePreview();
}

CBatchRenameDialog::~CBatchRenameDialog()
{
	QSettings s;
	s.setValue(SETTINGS_FIND_IS_REGEX, _findIsRegex->isChecked());
	s.setValue(SETTINGS_FIND_IS_CASE_SENSITIVE, _findIsCaseSensitive->isChecked());
	s.setValue(SETTINGS_COUNTER_START, _counterStart->value());
	s.setValue(SETTINGS_COUNTER_STEP, _counterStep->value());
	s.setValue(SETTINGS_COUNTER_DIGITS, _counterDigits->value());
}

void CBatchRenameDialog::reject()
{
	if (_renaming)
	{
		_cancelRequested = true;
		return;
	}

	QDialog::reject();
}

void CBatchRenameDialog::closeEvent(QCloseEvent* e)
{
	if (_renaming)
	{
		_cancelRequested = true;
		e->ignore();
		return;
	}

	QDialog::closeEvent(e);
}

BatchRenameRules CBatchRenameDialog::rules() const
{
	return {
		.nameMask = _nameMask->currentText(),
		.extensionMask = _extensionMask->currentText(),
		.letterCase = static_cast<LetterCase>(_letterCase->currentIndex()),
		.find = _find->currentText(),
		.replaceWith = _replaceWith->currentText(),
		.findIsRegex = _findIsRegex->isChecked(),
		.findIsCaseSensitive = _findIsCaseSensitive->isChecked(),
		.counterStart = _counterStart->value(),
		.counterStep = _counterStep->value(),
		.counterDigits = _counterDigits->value(),
	};
}

void CBatchRenameDialog::rulesChanged()
{
	for (Row& row : _rows)
		row.outcome.clear();
	_showingOutcomes = false;

	updatePreview();
}

void CBatchRenameDialog::updatePreview()
{
	const CBatchRenamePattern pattern{ rules() };
	const QString findError = pattern.findPatternError();
	_findError->setText(findError);
	_findError->setVisible(!findError.isEmpty());

	// Case-folded where filesystems usually ignore case
	const auto targetKey = [](const Row& row) {
		QString key = row.path.parent().value() % QLatin1Char('/') % row.newName;
#if defined(_WIN32) || defined(__APPLE__)
		key = key.toCaseFolded();
#endif
		return key;
	};

	QHash<QString, int> targetCounts;
	for (size_t i = 0; i < _rows.size(); ++i)
	{
		Row& row = _rows[i];
		row.newName = pattern.newName(sourceOf(row.item, row.path), static_cast<int>(i));
		++targetCounts[targetKey(row)];
	}

	bool anyRename = false;
	bool anyError = !findError.isEmpty();
	const QColor textColor = palette().color(QPalette::Text);
	const QColor dimmedColor = palette().color(QPalette::PlaceholderText);
	const QColor errorColor = Qt::red;
	for (size_t i = 0; i < _rows.size(); ++i)
	{
		const Row& row = _rows[i];
		const QString currentName = row.path.name();

		QString status;
		QColor statusColor = textColor;
		if (row.newName == currentName)
		{
			status = tr("Unchanged");
			statusColor = dimmedColor;
		}
		else if (const NameRejection rejection = checkNewEntryName(row.newName); rejection != NameRejection::None)
		{
			status = newNameRejectionText(rejection);
			statusColor = errorColor;
			anyError = true;
		}
		else if (targetCounts.value(targetKey(row)) > 1)
		{
			status = tr("Another item gets the same name");
			statusColor = errorColor;
			anyError = true;
		}
		else
			anyRename = true;

		if (!row.outcome.isEmpty())
		{
			status = row.outcome;
			statusColor = row.outcomeIsError ? errorColor : textColor;
		}

		const int tableRow = static_cast<int>(i);
		const auto setCellText = [this, tableRow](const int column, const QString& text) {
			QTableWidgetItem* cell = _preview->item(tableRow, column);
			cell->setText(text);
			cell->setToolTip(text); // The column may be too narrow for the text
		};
		setCellText(columns::Name, currentName);
		setCellText(columns::NewName, row.newName);
		setCellText(columns::Status, status);
		_preview->item(tableRow, columns::Status)->setForeground(statusColor);
	}

	// Showing outcomes: a click queued during the renaming must not start another one
	_applyButton->setEnabled(anyRename && !anyError && !_showingOutcomes);
	_undoButton->setEnabled(!_undo.empty());
}

void CBatchRenameDialog::applyRenaming()
{
	for (CHistoryComboBox* field : { _nameMask, _extensionMask, _find, _replaceWith })
		field->addToHistory(field->currentText());

	RowRenames renames;
	for (size_t i = 0; i < _rows.size(); ++i)
	{
		if (_rows[i].newName != _rows[i].path.name())
			renames.emplace_back(i, _rows[i].newName);
	}

	_undo = renameRows(renames);
	updatePreview();
}

void CBatchRenameDialog::undoRenaming()
{
	renameRows(std::exchange(_undo, {}));
	updatePreview();
}

CBatchRenameDialog::RowRenames CBatchRenameDialog::renameRows(const RowRenames& renames)
{
	QStringList paths;
	std::vector<BatchRenameItem> items;
	items.reserve(renames.size());
	for (const auto& [row, name] : renames)
	{
		paths.push_back(_rows[row].item.fullAbsolutePath());
		items.push_back({ .source = _rows[row].path, .newName = name });
	}

	auto* mainWindow = CMainWindow::get();
	assert_and_return_r(mainWindow, {});
	mainWindow->closeViewedFilesWithin(paths);

	setRenamingInProgress(true);
	QElapsedTimer sinceEventsProcessed;
	sinceEventsProcessed.start();
	// Processing events keeps this dialog painted and Cancel working. The other windows take no input: this dialog is modal.
	const auto results = batchRename(items, [this, &sinceEventsProcessed] {
		if (sinceEventsProcessed.elapsed() >= 50)
		{
			QCoreApplication::processEvents();
			sinceEventsProcessed.restart();
		}
		return !_cancelRequested;
	});
	setRenamingInProgress(false);

	RowRenames renamed;
	for (size_t i = 0; i < results.size(); ++i)
	{
		const auto& [rowIndex, name] = renames[i];
		Row& row = _rows[rowIndex];
		const std::optional<InlineRenameResult>& result = results[i];
		row.outcomeIsError = false;
		if (!result)
			row.outcome = tr("Cancelled");
		else if (result->status == InlineRenameStatus::Renamed)
		{
			renamed.emplace_back(rowIndex, row.path.name());
			row.path = row.path.parent().child(name);
			row.item = CFileSystemObject{ row.path.value() };
			row.outcome = tr("Renamed");
		}
		else if (result->status == InlineRenameStatus::NothingToDo)
			row.outcome = tr("Unchanged");
		else
		{
			row.outcome = tr("Failed: %1").arg(inlineRenameFailureText(*result));
			row.outcomeIsError = true;
		}
	}

	_showingOutcomes = true;
	return renamed;
}

void CBatchRenameDialog::setRenamingInProgress(const bool inProgress)
{
	_renaming = inProgress;
	_cancelRequested = false;
	_rulesPanel->setEnabled(!inProgress);
	_closeButton->setText(inProgress ? tr("Cancel") : tr("Close"));
	if (inProgress)
	{
		_applyButton->setEnabled(false);
		_undoButton->setEnabled(false);
	}
}
