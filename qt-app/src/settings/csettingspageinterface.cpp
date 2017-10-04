#include "csettingspageinterface.h"
#include "ui_csettingspageinterface.h"

#include "settings.h"
#include "settings/csettings.h"

DISABLE_COMPILER_WARNINGS
#include <QFontDialog>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

inline QString describeFont(const QFont& font)
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

	QFont font;
	if (font.fromString(CSettings().value(KEY_INTERFACE_FILE_LIST_FONT, INTERFACE_FILE_LIST_FONT_DEFAULT).toString()))
		_fontDialog->setCurrentFont(font);

	updateFontInfoLabel();

	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fontDialog->currentFont()));
	connect(ui->_btnSelectFileListFont, &QPushButton::clicked, this, [this]() {
		if (_fontDialog->exec() == QDialog::Accepted)
			updateFontInfoLabel();
	});

	ui->_cbRespectLastCursorPos->setChecked(CSettings().value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, false).toBool());
	ui->_cbSortingNumbersAfterLetters->setChecked(CSettings().value(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, false).toBool());
	ui->_cbDecoratedFolderIcons->setChecked(CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	CSettings().setValue(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, ui->_cbRespectLastCursorPos->isChecked());
	CSettings().setValue(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, ui->_cbSortingNumbersAfterLetters->isChecked());
	CSettings().setValue(KEY_INTERFACE_FILE_LIST_FONT, _fontDialog->currentFont().toString());
	CSettings().setValue(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, ui->_cbDecoratedFolderIcons->isChecked());
}

void CSettingsPageInterface::updateFontInfoLabel()
{
	ui->_lblCurrentFileListFontDescription->setFont(_fontDialog->currentFont());
	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fontDialog->currentFont()));
}
