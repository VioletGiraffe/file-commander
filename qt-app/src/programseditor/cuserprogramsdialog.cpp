#include "cuserprogramsdialog.h"
#include "userprogramsui.h"

#include "filesystemhelperfunctions.h"
#include "shell/cshellcommand.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "widgets/cpersistenceenabler.h"


DISABLE_COMPILER_WARNINGS
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QStringBuilder>
#include <QStringList>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
RESTORE_COMPILER_WARNINGS

#include <utility>

namespace {

enum Column { ProgramColumn, HotkeyColumn };
enum Role { CommandLineRole = Qt::UserRole, WorkingDirRole, CustomWorkingDirRole, EditBeforeRunningRole };

UserProgram programFromItem(const QTreeWidgetItem* item)
{
	return {
		.name = item->text(ProgramColumn),
		.commandLine = item->data(ProgramColumn, CommandLineRole).toString(),
		.workingDir = static_cast<UserProgram::WorkingDir>(item->data(ProgramColumn, WorkingDirRole).toInt()),
		.customWorkingDir = item->data(ProgramColumn, CustomWorkingDirRole).toString(),
		.editBeforeRunning = item->data(ProgramColumn, EditBeforeRunningRole).toBool(),
	};
}

// Not the icon: it needs a search through PATH
void storeProgramInItem(QTreeWidgetItem* item, const UserProgram& program)
{
	item->setText(ProgramColumn, program.name);
	item->setData(ProgramColumn, CommandLineRole, program.commandLine);
	item->setData(ProgramColumn, WorkingDirRole, static_cast<int>(program.workingDir));
	item->setData(ProgramColumn, CustomWorkingDirRole, program.customWorkingDir);
	item->setData(ProgramColumn, EditBeforeRunningRole, program.editBeforeRunning);
}

QString newProgramName()
{
	return CUserProgramsDialog::tr("New program");
}

// Folders are skipped: there is nothing to run
QStringList droppedFiles(const QMimeData* mimeData)
{
	QStringList files;
	for (const QUrl& url : mimeData->urls())
	{
		if (url.isLocalFile() && QFileInfo{ url.toLocalFile() }.isFile())
			files.push_back(url.toLocalFile());
	}

	return files;
}

} // namespace

CUserProgramsDialog::CUserProgramsDialog(const std::vector<UserProgram>& programs, PlaceholderValues panelState, RunProgram runProgram, QWidget* parent) :
	QDialog(parent),
	_panelState{ std::move(panelState) },
	_runProgram{ std::move(runProgram) }
{
	setWindowTitle(tr("Programs"));
	// The list ignores drags from elsewhere, so Qt passes them up to the dialog
	setAcceptDrops(true);

	auto* splitter = new QSplitter{ Qt::Horizontal };
	splitter->setChildrenCollapsible(false);
	splitter->addWidget(createListPane());
	splitter->addWidget(createDetailsPane());
	splitter->setStretchFactor(1, 1);

	auto* buttons = new QDialogButtonBox{ QDialogButtonBox::Ok | QDialogButtonBox::Cancel };
	connect(buttons, &QDialogButtonBox::accepted, this, &CUserProgramsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &CUserProgramsDialog::reject);

	auto* layout = new QVBoxLayout{ this };
	layout->addWidget(splitter);
	layout->addWidget(buttons);

	resize(900, 420);
	enablePersistence(this, QStringLiteral("UI/ProgramsDialog"), CPersistenceEnabler::Delayed{ true }, CPersistenceEnabler::SetDefaultSize{ false });

	for (const UserProgram& program : programs)
		insertProgram(program, _list->topLevelItemCount());

	if (_list->topLevelItemCount() > 0)
		_list->setCurrentItem(_list->topLevelItem(0));
	else
		showDetails(nullptr);
}

std::vector<UserProgram> CUserProgramsDialog::programs() const
{
	std::vector<UserProgram> result;
	result.reserve(static_cast<size_t>(_list->topLevelItemCount()));
	for (int row = 0; row < _list->topLevelItemCount(); ++row)
		result.push_back(programFromItem(_list->topLevelItem(row)));

	return result;
}

void CUserProgramsDialog::accept()
{
	for (int row = 0; row < _list->topLevelItemCount(); ++row)
	{
		QTreeWidgetItem* item = _list->topLevelItem(row);
		const UserProgram program = programFromItem(item);

		QWidget* invalidField = nullptr;
		QString problem;
		if (program.name.trimmed().isEmpty())
		{
			invalidField = _name;
			problem = tr("Enter a name for the program.");
		}
		else if (program.commandLine.trimmed().isEmpty())
		{
			invalidField = _commandLine;
			problem = tr("Enter the command line.");
		}
		else if (program.workingDir == UserProgram::WorkingDir::Custom && program.customWorkingDir.trimmed().isEmpty())
		{
			invalidField = _customWorkingDir;
			problem = tr("Enter the working folder.");
		}
		else
			continue;

		_list->setCurrentItem(item);
		QMessageBox::warning(this, windowTitle(), problem);
		invalidField->setFocus();
		return;
	}

	QDialog::accept();
}

void CUserProgramsDialog::dragEnterEvent(QDragEnterEvent* event)
{
	if (!droppedFiles(event->mimeData()).empty())
		event->acceptProposedAction();
}

void CUserProgramsDialog::dropEvent(QDropEvent* event)
{
	const QStringList files = droppedFiles(event->mimeData());
	if (files.empty())
		return;

	for (const QString& file : files)
	{
		const UserProgram program{ .name = QFileInfo{ file }.completeBaseName(), .commandLine = shellQuotedPath(toNativeSeparators(file)) };
		_list->setCurrentItem(insertProgram(program, _list->topLevelItemCount()));
	}

	event->acceptProposedAction();

	// A drop from another application leaves this window in the background
	raise();
	activateWindow();
}

QWidget* CUserProgramsDialog::createListPane()
{
	_list = new QTreeWidget;
	_list->setHeaderLabels({ tr("Program"), tr("Hotkey") });
	_list->header()->setStretchLastSection(false);
	_list->header()->setSectionResizeMode(ProgramColumn, QHeaderView::Stretch);
	_list->header()->setSectionResizeMode(HotkeyColumn, QHeaderView::ResizeToContents);
	_list->setRootIsDecorated(false);
	_list->setUniformRowHeights(true);
	_list->setDragDropMode(QAbstractItemView::InternalMove);
	_list->setDefaultDropAction(Qt::MoveAction);

	// Hotkeys follow the row positions
	connect(_list->model(), &QAbstractItemModel::rowsInserted, this, &CUserProgramsDialog::updateHotkeys);
	connect(_list->model(), &QAbstractItemModel::rowsRemoved, this, &CUserProgramsDialog::updateHotkeys);
	connect(_list->model(), &QAbstractItemModel::rowsMoved, this, &CUserProgramsDialog::updateHotkeys);
	connect(_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current) { showDetails(current); });

	auto* btnAdd = new QPushButton{ tr("&Add") };
	connect(btnAdd, &QPushButton::clicked, this, &CUserProgramsDialog::addNewProgram);

	_btnDuplicate = new QPushButton{ tr("D&uplicate") };
	connect(_btnDuplicate, &QPushButton::clicked, this, &CUserProgramsDialog::duplicateCurrentProgram);

	_btnRemove = new QPushButton{ tr("&Remove") };
	connect(_btnRemove, &QPushButton::clicked, this, &CUserProgramsDialog::removeCurrentProgram);

	_btnMoveUp = new QToolButton;
	_btnMoveUp->setText(QStringLiteral("↑"));
	_btnMoveUp->setToolTip(tr("Move up: the position in the list sets the hotkey"));
	connect(_btnMoveUp, &QToolButton::clicked, this, [this] { moveCurrentProgram(-1); });

	_btnMoveDown = new QToolButton;
	_btnMoveDown->setText(QStringLiteral("↓"));
	_btnMoveDown->setToolTip(tr("Move down: the position in the list sets the hotkey"));
	connect(_btnMoveDown, &QToolButton::clicked, this, [this] { moveCurrentProgram(1); });

	auto* buttonsLayout = new QHBoxLayout;
	buttonsLayout->addWidget(btnAdd);
	buttonsLayout->addWidget(_btnDuplicate);
	buttonsLayout->addWidget(_btnRemove);
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(_btnMoveUp);
	buttonsLayout->addWidget(_btnMoveDown);

	auto* pane = new QWidget;
	auto* layout = new QVBoxLayout{ pane };
	layout->setContentsMargins({});
	layout->addWidget(_list);
	layout->addLayout(buttonsLayout);
	return pane;
}

QWidget* CUserProgramsDialog::createDetailsPane()
{
	_name = new QLineEdit;
	connect(_name, &QLineEdit::textChanged, this, &CUserProgramsDialog::storeDetails);

	_commandLine = new QLineEdit;
	connect(_commandLine, &QLineEdit::textChanged, this, &CUserProgramsDialog::storeDetails);
	connect(_commandLine, &QLineEdit::editingFinished, this, &CUserProgramsDialog::updateCurrentIcon);

	auto* btnBrowseProgram = new QToolButton;
	btnBrowseProgram->setText(tr("&Browse..."));
	btnBrowseProgram->setToolTip(tr("Insert the path of a program at the cursor"));
	connect(btnBrowseProgram, &QToolButton::clicked, this, &CUserProgramsDialog::browseForProgram);

	const std::pair<Placeholder, QString> placeholders[]{
		{ Placeholder::File,      tr("Path of the item under the cursor") },
		{ Placeholder::Name,      tr("Name of the item under the cursor") },
		{ Placeholder::Selection, tr("Paths of the selected items, or of the item under the cursor") },
		{ Placeholder::Dir,       tr("Current panel's folder") },
		{ Placeholder::Other,     tr("Other panel's folder") },
		{ Placeholder::OtherFile, tr("Path of the item under the cursor in the other panel") },
	};

	auto* btnInsertPlaceholder = new QToolButton;
	btnInsertPlaceholder->setText(tr("&Insert"));
	btnInsertPlaceholder->setToolTip(tr("Insert a placeholder at the cursor: it is replaced with a path when the program runs"));
	btnInsertPlaceholder->setPopupMode(QToolButton::InstantPopup);
	auto* placeholderMenu = new QMenu{ btnInsertPlaceholder };
	for (const auto& [placeholder, description] : placeholders)
	{
		const QString token = placeholderToken(placeholder);
		// QMenu shows the text after '\t' in the shortcut column
		const QAction* action = placeholderMenu->addAction(QString{ description % '\t' % token });
		connect(action, &QAction::triggered, this, [this, token] { insertIntoCommandLine(token); });
	}
	btnInsertPlaceholder->setMenu(placeholderMenu);

	auto* commandLineLayout = new QHBoxLayout;
	commandLineLayout->addWidget(_commandLine);
	commandLineLayout->addWidget(btnBrowseProgram);
	commandLineLayout->addWidget(btnInsertPlaceholder);

	_preview = new QLabel;
	_preview->setTextFormat(Qt::PlainText);
	_preview->setWordWrap(true);
	_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);

	_workingDir = new QComboBox;
	_workingDir->addItem(tr("Current panel's folder"), static_cast<int>(UserProgram::WorkingDir::CurrentPanel));
	_workingDir->addItem(tr("Other panel's folder"), static_cast<int>(UserProgram::WorkingDir::OtherPanel));
	_workingDir->addItem(tr("Custom folder"), static_cast<int>(UserProgram::WorkingDir::Custom));
	connect(_workingDir, &QComboBox::currentIndexChanged, this, [this] {
		storeDetails();
		updateCurrentIcon();
	});

	_customWorkingDir = new QLineEdit;
	connect(_customWorkingDir, &QLineEdit::textChanged, this, &CUserProgramsDialog::storeDetails);
	connect(_customWorkingDir, &QLineEdit::editingFinished, this, &CUserProgramsDialog::updateCurrentIcon);

	_btnBrowseWorkingDir = new QToolButton;
	_btnBrowseWorkingDir->setText(QStringLiteral("..."));
	_btnBrowseWorkingDir->setToolTip(tr("Choose the working folder"));
	connect(_btnBrowseWorkingDir, &QToolButton::clicked, this, &CUserProgramsDialog::browseForWorkingDir);

	auto* workingDirLayout = new QHBoxLayout;
	workingDirLayout->addWidget(_workingDir);
	workingDirLayout->addWidget(_customWorkingDir, 1);
	workingDirLayout->addWidget(_btnBrowseWorkingDir);

	_editBeforeRunning = new QCheckBox{ tr("&Edit the command line before running") };
	connect(_editBeforeRunning, &QCheckBox::toggled, this, &CUserProgramsDialog::storeDetails);

	_hotkey = new QLabel;

	_btnTestRun = new QPushButton{ tr("&Test run") };
	connect(_btnTestRun, &QPushButton::clicked, this, [this] {
		if (QTreeWidgetItem* item = _list->currentItem())
			_runProgram(programFromItem(item));
	});

	auto* testRunLayout = new QHBoxLayout;
	testRunLayout->addWidget(_btnTestRun);
	testRunLayout->addStretch();

	_details = new QWidget;
	auto* layout = new QFormLayout{ _details };
	layout->setContentsMargins(layout->contentsMargins().left(), 0, 0, 0);
	// A row whose field is a layout gets no buddy for its label
	auto* commandLineLabel = new QLabel{ tr("&Command line:") };
	commandLineLabel->setBuddy(_commandLine);
	auto* workingDirLabel = new QLabel{ tr("&Working folder:") };
	workingDirLabel->setBuddy(_workingDir);

	layout->addRow(tr("&Name:"), _name);
	layout->addRow(commandLineLabel, commandLineLayout);
	layout->addRow(tr("Preview:"), _preview);
	layout->addRow(workingDirLabel, workingDirLayout);
	layout->addRow(QString{}, _editBeforeRunning);
	layout->addRow(tr("Hotkey:"), _hotkey);
	layout->addRow(QString{}, testRunLayout);
	return _details;
}

QTreeWidgetItem* CUserProgramsDialog::insertProgram(const UserProgram& program, const int row)
{
	auto* item = new QTreeWidgetItem;
	// Not drop-enabled: a drop onto a row would nest the dragged one under it
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
	storeProgramInItem(item, program);
	item->setIcon(ProgramColumn, userProgramIcon(program));
	_list->insertTopLevelItem(row, item);
	return item;
}

void CUserProgramsDialog::addNewProgram()
{
	_list->setCurrentItem(insertProgram(UserProgram{ .name = newProgramName() }, _list->topLevelItemCount()));
	_name->setFocus();
	_name->selectAll();
}

void CUserProgramsDialog::duplicateCurrentProgram()
{
	QTreeWidgetItem* current = _list->currentItem();
	assert_and_return_r(current, );

	UserProgram copy = programFromItem(current);
	copy.name = tr("%1 (copy)").arg(copy.name);
	_list->setCurrentItem(insertProgram(copy, _list->indexOfTopLevelItem(current) + 1));
}

void CUserProgramsDialog::removeCurrentProgram()
{
	delete _list->currentItem();
}

void CUserProgramsDialog::moveCurrentProgram(const int offset)
{
	QTreeWidgetItem* item = _list->currentItem();
	assert_and_return_r(item, );

	const int newRow = _list->indexOfTopLevelItem(item) + offset;
	assert_and_return_r(newRow >= 0 && newRow < _list->topLevelItemCount(), );

	_list->takeTopLevelItem(_list->indexOfTopLevelItem(item));
	_list->insertTopLevelItem(newRow, item);
	_list->setCurrentItem(item);
}

void CUserProgramsDialog::showDetails(QTreeWidgetItem* item)
{
	const UserProgram program = item ? programFromItem(item) : UserProgram{};

	_showingDetails = true;
	_name->setText(program.name);
	_commandLine->setText(program.commandLine);
	_workingDir->setCurrentIndex(_workingDir->findData(static_cast<int>(program.workingDir)));
	_customWorkingDir->setText(program.customWorkingDir);
	_editBeforeRunning->setChecked(program.editBeforeRunning);
	_showingDetails = false;

	_details->setEnabled(item != nullptr);
	updatePreview();
	updateHotkeys();
}

UserProgram CUserProgramsDialog::programInDetails() const
{
	return {
		.name = _name->text(),
		.commandLine = _commandLine->text(),
		.workingDir = static_cast<UserProgram::WorkingDir>(_workingDir->currentData().toInt()),
		.customWorkingDir = _customWorkingDir->text(),
		.editBeforeRunning = _editBeforeRunning->isChecked(),
	};
}

void CUserProgramsDialog::storeDetails()
{
	QTreeWidgetItem* item = _list->currentItem();
	if (_showingDetails || !item)
		return;

	storeProgramInItem(item, programInDetails());
	updatePreview();
	updateEnabledState();
}

void CUserProgramsDialog::updateCurrentIcon()
{
	if (_showingDetails)
		return;

	if (QTreeWidgetItem* item = _list->currentItem())
		item->setIcon(ProgramColumn, userProgramIcon(programFromItem(item)));
}

void CUserProgramsDialog::updatePreview()
{
	const QTreeWidgetItem* item = _list->currentItem();
	const UserProgram program = item ? programFromItem(item) : UserProgram{};
	if (program.commandLine.trimmed().isEmpty())
	{
		_preview->clear();
		return;
	}

	const auto commandLine = expandPlaceholders(program.commandLine, _panelState);
	if (!commandLine)
	{
		_preview->setText(QString{ QStringLiteral("⚠ ") % placeholderErrorText(commandLine.error()) });
		return;
	}

	const QString workingDir = workingDirFor(program, _panelState);
	if (const qsizetype maxLength = CShellCommand::maxCommandLength(workingDir); commandLine->size() > maxLength)
	{
		_preview->setText(QString{ QStringLiteral("⚠ ") % commandLineTooLongText(commandLine->size(), maxLength) });
		return;
	}

	QString preview = *commandLine;
	if (!workingDir.isEmpty())
	{
		preview += '\n';
		preview += tr("in %1").arg(workingDir);
	}
	_preview->setText(preview);
}

void CUserProgramsDialog::updateHotkeys()
{
	for (int row = 0; row < _list->topLevelItemCount(); ++row)
		_list->topLevelItem(row)->setText(HotkeyColumn, userProgramShortcut(static_cast<size_t>(row)).toString(QKeySequence::NativeText));

	const int currentRow = _list->indexOfTopLevelItem(_list->currentItem());
	if (currentRow < 0)
		_hotkey->clear();
	else if (const QKeySequence shortcut = userProgramShortcut(static_cast<size_t>(currentRow)); !shortcut.isEmpty())
		_hotkey->setText(shortcut.toString(QKeySequence::NativeText));
	else
		_hotkey->setText(tr("None: only the first 12 programs have one"));

	updateEnabledState();
}

void CUserProgramsDialog::updateEnabledState()
{
	QTreeWidgetItem* item = _list->currentItem();
	const int row = _list->indexOfTopLevelItem(item);

	_btnDuplicate->setEnabled(item != nullptr);
	_btnRemove->setEnabled(item != nullptr);
	_btnMoveUp->setEnabled(row > 0);
	_btnMoveDown->setEnabled(item != nullptr && row < _list->topLevelItemCount() - 1);
	_btnTestRun->setEnabled(item != nullptr && !_commandLine->text().trimmed().isEmpty());

	const bool customWorkingDir = _workingDir->currentData().toInt() == static_cast<int>(UserProgram::WorkingDir::Custom);
	_customWorkingDir->setEnabled(customWorkingDir);
	_btnBrowseWorkingDir->setEnabled(customWorkingDir);
}

void CUserProgramsDialog::browseForProgram()
{
#ifdef _WIN32
	const QString filter = tr("Programs (*.exe *.com *.bat *.cmd);;All files (*)");
#else
	const QString filter;
#endif
	const QString path = QFileDialog::getOpenFileName(this, tr("Choose a program"), {}, filter);
	if (path.isEmpty())
		return;

	if (_name->text().trimmed().isEmpty() || _name->text() == newProgramName())
		_name->setText(QFileInfo{ path }.completeBaseName());

	insertIntoCommandLine(shellQuotedPath(toNativeSeparators(path)));
}

void CUserProgramsDialog::browseForWorkingDir()
{
	const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose the working folder"), _customWorkingDir->text());
	if (folder.isEmpty())
		return;

	_customWorkingDir->setText(toNativeSeparators(folder));
	updateCurrentIcon();
}

void CUserProgramsDialog::insertIntoCommandLine(const QString& text)
{
	_commandLine->insert(text);
	_commandLine->setFocus();
	updateCurrentIcon();
}
