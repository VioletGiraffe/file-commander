#include "cfinddialog.h"
#include "ui_cfinddialog.h"
#include "settings/csettings.h"

#define SETTINGS_SEARCH_EXPRESSION "Expression"

#define SETTINGS_REGEX             "FindDialog/Regex"
#define SETTINGS_BACKWARDS         "SearchBackwards"
#define SETTINGS_CASE_SENSITIVE    "CaseSensitive"
#define SETTINGS_WHOLE_WORDS       "WholeWords"

#define SETTINGS_GEOMETRY          "Geometry"

CFindDialog::CFindDialog(QWidget *parent, const QString& settingsRootCategory) :
	QDialog(parent),
	_settingsRootCategory(settingsRootCategory),
	ui(new Ui::CFindDialog)
{
	ui->setupUi(this);

	connect(ui->_btnCancel, SIGNAL(clicked()), SLOT(reject()));
	connect(ui->_btnFind, SIGNAL(clicked()), SLOT(accept()));
	connect(ui->_btnFind, SIGNAL(clicked()), SIGNAL(find()));
	connect(ui->_btnFindNext, SIGNAL(clicked()), SIGNAL(findNext()));

	if (!_settingsRootCategory.isEmpty())
	{
		CSettings s;
		ui->_searchText->setText(s.value(_settingsRootCategory + SETTINGS_SEARCH_EXPRESSION).toString());
		ui->_cbSearchBackwards->setChecked(s.value(_settingsRootCategory + SETTINGS_BACKWARDS).toBool());
		ui->_cbCaseSensitive->setChecked(s.value(_settingsRootCategory + SETTINGS_CASE_SENSITIVE).toBool());
		ui->_cbRegex->setChecked(s.value(_settingsRootCategory + SETTINGS_REGEX).toBool());
		ui->_cbWholeWords->setChecked(s.value(_settingsRootCategory + SETTINGS_WHOLE_WORDS).toBool());
	}

#if QT_VERSION < QT_VERSION_CHECK(5,3,0)
	ui->_cbRegex->setVisible(false);
#endif
}

CFindDialog::~CFindDialog()
{
	saveSearchSettings();
	delete ui;
}

QString CFindDialog::searchExpression() const
{
	return ui->_searchText->text();
}

bool CFindDialog::regex() const
{
	return ui->_cbRegex->isChecked();
}

bool CFindDialog::searchBackwards() const
{
	return ui->_cbSearchBackwards->isChecked();
}

bool CFindDialog::wholeWords() const
{
	return ui->_cbWholeWords->isChecked();
}

bool CFindDialog::caseSensitive() const
{
	return ui->_cbCaseSensitive->isChecked();
}

void CFindDialog::accept()
{
	QDialog::accept();
	saveSearchSettings();
}

void CFindDialog::showEvent(QShowEvent * e)
{
	ui->_searchText->selectAll();
	ui->_searchText->setFocus();

	if (!_settingsRootCategory.isEmpty())
		restoreGeometry(CSettings().value(_settingsRootCategory + SETTINGS_GEOMETRY).toByteArray());

	QDialog::showEvent(e);
}

void CFindDialog::hideEvent(QHideEvent * e)
{
	if (!_settingsRootCategory.isEmpty())
		CSettings().setValue(_settingsRootCategory + SETTINGS_GEOMETRY, saveGeometry());

	QDialog::hideEvent(e);
}

void CFindDialog::saveSearchSettings() const
{
	CSettings s;
	s.setValue(_settingsRootCategory + SETTINGS_SEARCH_EXPRESSION, searchExpression());
	s.setValue(_settingsRootCategory + SETTINGS_BACKWARDS, searchBackwards());
	s.setValue(_settingsRootCategory + SETTINGS_CASE_SENSITIVE, caseSensitive());
	s.setValue(_settingsRootCategory + SETTINGS_REGEX, regex());
	s.setValue(_settingsRootCategory + SETTINGS_WHOLE_WORDS, wholeWords());
}
