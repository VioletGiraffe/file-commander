#include "csettingspageinterface.h"

#include "themes/theme.h"

#include "settings.h"


// Submodule includes
#include "theme/cthemecontroller.h"


DISABLE_COMPILER_WARNINGS
#include "ui_csettingspageinterface.h"

#include <QFontDialog>
#include <QSettings>
#include <QStringBuilder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

static inline QString describeFont(const QFont& font)
{
	return font.family() % ", " % QString::number(font.pointSize()) % ", " % font.styleName();
}

CSettingsPageInterface::CSettingsPageInterface(QWidget *parent) :
	CSettingsPage(parent),
	_fontDialog(std::make_unique<QFontDialog>(this)),
	ui(new Ui::CSettingsPageInterface)
{
	ui->setupUi(this);
	_fontDialog->resize(_fontDialog->sizeHint().width(), _fontDialog->sizeHint().height() * 3/2);

	QSettings s;

	// A malformed value leaves the application's font; the file list ignores such a value too
	_fileListFont.fromString(s.value(KEY_INTERFACE_FILE_LIST_FONT, INTERFACE_FILE_LIST_FONT_DEFAULT).toString());

	updateFontInfoLabel();
	// A stylesheet change restores the font the label had when first polished
	connect(&CThemeController::instance(), &CThemeController::themeChanged, this, &CSettingsPageInterface::updateFontInfoLabel);

	connect(ui->_btnSelectFileListFont, &QPushButton::clicked, this, [this]() {
		_fontDialog->setCurrentFont(_fileListFont);
		if (_fontDialog->exec() != QDialog::Accepted)
			return;

		_fileListFont = _fontDialog->selectedFont();
		updateFontInfoLabel();
	});

	ui->_cbRespectLastCursorPos->setChecked(s.value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, INTERFACE_RESPECT_LAST_CURSOR_POS_DEFAULT).toBool());
	ui->_cbDecoratedFolderIcons->setChecked(s.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());
	ui->_cbColumnSeparators->setChecked(s.value(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, INTERFACE_SHOW_COLUMN_SEPARATORS_DEFAULT).toBool());

	QStringList lightThemeNames, darkThemeNames;
	for (const Theme& theme : allThemes())
		(theme.dark ? darkThemeNames : lightThemeNames).push_back(theme.name);
	_themeControls.emplace(*ui->_themeForm, lightThemeNames, darkThemeNames);
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	QSettings s;
	s.setValue(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, ui->_cbRespectLastCursorPos->isChecked());
	s.setValue(KEY_INTERFACE_FILE_LIST_FONT, _fileListFont.toString());
	s.setValue(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, ui->_cbDecoratedFolderIcons->isChecked());
	s.setValue(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, ui->_cbColumnSeparators->isChecked());
}

void CSettingsPageInterface::rejectSettings()
{
	_themeControls->restoreInitialSelection();
}

void CSettingsPageInterface::updateFontInfoLabel()
{
	ui->_lblCurrentFileListFontDescription->setFont(_fileListFont);
	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fileListFont));
}
