#include "csettingspageedit.h"
#include "ui_csettingspageedit.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QFileDialog>
#include <QSettings>
RESTORE_COMPILER_WARNINGS


CSettingsPageEdit::CSettingsPageEdit(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageEdit)
{
	ui->setupUi(this);
	connect(ui->_btnEditorBrowse, &QPushButton::clicked, this, &CSettingsPageEdit::browseForEditor);

	ui->_editorNameLine->setText(QSettings().value(KEY_EDITOR_PATH).toString());
}

CSettingsPageEdit::~CSettingsPageEdit()
{
	delete ui;
}

void CSettingsPageEdit::acceptSettings()
{
	QSettings().setValue(KEY_EDITOR_PATH, ui->_editorNameLine->text());
}

void CSettingsPageEdit::browseForEditor()
{
#ifdef _WIN32
	const QString mask(tr("Executable files (*.exe *.cmd *.bat)"));
#else
	const QString mask;
#endif
	const QString result = QFileDialog::getOpenFileName(this, tr("Browse for editor program"), QFileInfo(ui->_editorNameLine->text()).absolutePath(), mask);
	if (!result.isEmpty())
		ui->_editorNameLine->setText(result);
}
