#include "cbatchrenamedialog.h"

#include "cmainwindow.h"
#include "themes/theme.h"

#include "ccontroller.h"
#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/chistorycombobox.h"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <vector>

namespace columns {
	enum Columns {
		Name, NewName, Status, Count
	};
}

CBatchRenameDialog::CBatchRenameDialog(QWidget* parent, const std::vector<CFileSystemObject>& items) :
	QDialog(parent),
	_session(items)
{
	setWindowTitle(tr("Batch rename"));

	_rulesPanel = new QWidget;
	_nameMask = new CHistoryComboBox(_rulesPanel);
	_extensionMask = new CHistoryComboBox(_rulesPanel);
	_find = new CHistoryComboBox(_rulesPanel);
	_replaceWith = new CHistoryComboBox(_rulesPanel);
	_findIsRegex = new QCheckBox(tr("Regular expression"));
	_findIsCaseSensitive = new QCheckBox(tr("Case-sensitive"));
	_findError = new QLabel;
	_findError->hide();

	// In LetterCase order
	_letterCase = new QComboBox;
	_letterCase->addItems({ tr("Unchanged"), tr("lowercase"), tr("UPPERCASE"), tr("Title Case") });

	const auto newCounterOption = [](const CBatchRenameSession::Range range) {
		auto* option = new QSpinBox;
		option->setRange(range.min, range.max);
		return option;
	};
	_counterStart = newCounterOption(CBatchRenameSession::counterStartRange);
	_counterStep = newCounterOption(CBatchRenameSession::counterStepRange);
	_counterDigits = newCounterOption(CBatchRenameSession::counterDigitsRange);

	// The fields that change names on their own start at the default rules; the history keeps what was used before
	const BatchRenameRules initial = CBatchRenameSession::initialRules();
	// Before the histories load: a combo box drops the items past its maximum
	for (CHistoryComboBox* field : { _nameMask, _extensionMask, _find, _replaceWith })
		field->setMaxCount(FIELD_HISTORY_MAX_ENTRIES);
	_nameMask->setSettingsKeyForHistoryPersistence(KEY_BATCH_RENAME_NAME_MASK_HISTORY);
	_extensionMask->setSettingsKeyForHistoryPersistence(KEY_BATCH_RENAME_EXTENSION_MASK_HISTORY);
	_find->setSettingsKeyForHistoryPersistence(KEY_BATCH_RENAME_FIND_HISTORY);
	_replaceWith->setSettingsKeyForHistoryPersistence(KEY_BATCH_RENAME_REPLACE_WITH_HISTORY);
	_nameMask->setCurrentText(initial.nameMask);
	_extensionMask->setCurrentText(initial.extensionMask);
	_find->setCurrentText(initial.find);
	_replaceWith->setCurrentText(initial.replaceWith);

	_findIsRegex->setChecked(initial.findIsRegex);
	_findIsCaseSensitive->setChecked(initial.findIsCaseSensitive);
	_counterStart->setValue(initial.counterStart);
	_counterStep->setValue(initial.counterStep);
	_counterDigits->setValue(initial.counterDigits);

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

	_preview = new QTableWidget(static_cast<int>(_session.rows().size()), columns::Count);
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
	connect(_undoButton, &QPushButton::clicked, this, [this] { runStep(CBatchRenameSession::Step::Undo); });
	connect(_closeButton, &QPushButton::clicked, this, &CBatchRenameDialog::reject);

	enablePersistence(this, QSL("UI/BatchRenameDialog"));
	rulesChanged();
}

CBatchRenameDialog::~CBatchRenameDialog()
{
	CBatchRenameSession::saveOptions(rules());
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
	_session.setRules(rules());
	showSession();
}

void CBatchRenameDialog::showSession()
{
	const QColor textColor = palette().color(QPalette::Text);
	const QColor dimmedColor = palette().color(QPalette::PlaceholderText);
	const QColor errorColor = appliedTheme().palette.errorText;

	const QString& findError = _session.findPatternError();
	_findError->setText(findError);
	_findError->setVisible(!findError.isEmpty());
	QPalette findErrorPalette = _findError->palette();
	findErrorPalette.setColor(QPalette::WindowText, errorColor);
	_findError->setPalette(findErrorPalette);

	const std::vector<CBatchRenameSession::Row>& rows = _session.rows();
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const CBatchRenameSession::Row& row = rows[i];
		const CBatchRenameSession::Status status = CBatchRenameSession::statusOf(row);

		const int tableRow = static_cast<int>(i);
		const auto setCellText = [this, tableRow](const int column, const QString& text) {
			QTableWidgetItem* cell = _preview->item(tableRow, column);
			cell->setText(text);
			cell->setToolTip(text); // The column may be too narrow for the text
		};
		setCellText(columns::Name, row.path.name());
		setCellText(columns::NewName, row.newName);
		setCellText(columns::Status, status.text);
		const QColor statusColor = status.tone == CBatchRenameSession::Tone::Error ? errorColor :
			status.tone == CBatchRenameSession::Tone::Dimmed ? dimmedColor : textColor;
		_preview->item(tableRow, columns::Status)->setForeground(statusColor);
	}

	_applyButton->setEnabled(_session.canRun(CBatchRenameSession::Step::Apply));
	_undoButton->setEnabled(_session.canRun(CBatchRenameSession::Step::Undo));
}

void CBatchRenameDialog::applyRenaming()
{
	for (CHistoryComboBox* field : { _nameMask, _extensionMask, _find, _replaceWith })
		field->addToHistory(field->currentText());

	runStep(CBatchRenameSession::Step::Apply);
}

void CBatchRenameDialog::runStep(const CBatchRenameSession::Step step)
{
	auto* mainWindow = CMainWindow::get();
	assert_and_return_r(mainWindow, );
	mainWindow->closeViewedFilesWithin(_session.pathsRenamedBy(step));

	setRenamingInProgress(true);
	QElapsedTimer sinceEventsProcessed;
	sinceEventsProcessed.start();
	// Processing events keeps this dialog painted and Cancel working. The other windows take no input: this dialog is modal.
	const auto renamedPaths = _session.run(step, [this, &sinceEventsProcessed] {
		if (sinceEventsProcessed.elapsed() >= 50)
		{
			QCoreApplication::processEvents();
			sinceEventsProcessed.restart();
		}
		return !_cancelRequested;
	});
	setRenamingInProgress(false);
	CController::get().itemsRenamed(renamedPaths);

	showSession();
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
