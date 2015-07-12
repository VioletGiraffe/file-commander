#include "csettingspageedit.h"
#include "ui_csettingspageedit.h"
#include "settings.h"
#include "settings/csettings.h"

#include <QFileDialog>


CSettingsPageEdit::CSettingsPageEdit(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageEdit)
{
	ui->setupUi(this);
	connect(ui->_btnEditorBrowse, &QPushButton::clicked, this, &CSettingsPageEdit::browseForEditor);

	ui->_editorNameLine->setText(CSettings().value(KEY_EDITOR_PATH).toString());
}

CSettingsPageEdit::~CSettingsPageEdit()
{
	delete ui;
}

void CSettingsPageEdit::acceptSettings()
{
	CSettings().setValue(KEY_EDITOR_PATH, ui->_editorNameLine->text());
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
