#include "cfinddialog.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "widgets/cpersistenceenabler.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfinddialog.h"

#include <QLineEdit>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#define SETTINGS_SEARCH_EXPRESSION_LIST QSL("Expressions")
#define SETTINGS_REGEX                  QSL("Regex")
#define SETTINGS_BACKWARDS              QSL("SearchBackwards")
#define SETTINGS_CASE_SENSITIVE         QSL("CaseSensitive")
#define SETTINGS_WHOLE_WORDS            QSL("WholeWords")

CFindDialog::CFindDialog(QWidget *parent, QString settingsRootCategory) :
	QDialog(parent),
	_settingsRootCategory{std::move(settingsRootCategory)},
	ui(new Ui::CFindDialog)
{
	ui->setupUi(this);

	ui->_searchText->enableAutoSave(_settingsRootCategory + SETTINGS_SEARCH_EXPRESSION_LIST);

	connect(ui->_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(ui->_btnFind, &QPushButton::clicked, this, &QDialog::accept);
	connect(ui->_btnFind, &QPushButton::clicked, this, &CFindDialog::find);
	connect(ui->_btnFindNext, &QPushButton::clicked, this, &CFindDialog::findNext);

	connect(ui->_searchText, &CHistoryComboBox::itemActivated, ui->_btnFind, &QPushButton::click);

	if (!_settingsRootCategory.isEmpty())
	{
		// No default size: it would open this small dialog at half the screen when nothing is stored yet
		enablePersistence(this, _settingsRootCategory, CPersistenceEnabler::Delayed{ true }, CPersistenceEnabler::SetDefaultSize{ false });

		QSettings s;
		ui->_cbSearchBackwards->setChecked(s.value(_settingsRootCategory + SETTINGS_BACKWARDS).toBool());
		ui->_cbCaseSensitive->setChecked(s.value(_settingsRootCategory + SETTINGS_CASE_SENSITIVE).toBool());
		ui->_cbRegex->setChecked(s.value(_settingsRootCategory + SETTINGS_REGEX).toBool());
		ui->_cbWholeWords->setChecked(s.value(_settingsRootCategory + SETTINGS_WHOLE_WORDS).toBool());
	}
}

CFindDialog::~CFindDialog()
{
	saveSearchSettings();
	delete ui;
}

QString CFindDialog::searchExpression() const
{
	return ui->_searchText->currentText();
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
	ui->_searchText->lineEdit()->selectAll();
	ui->_searchText->lineEdit()->setFocus();

	QDialog::showEvent(e);
}

void CFindDialog::saveSearchSettings() const
{
	QSettings s;
	s.setValue(_settingsRootCategory + SETTINGS_BACKWARDS, searchBackwards());
	s.setValue(_settingsRootCategory + SETTINGS_CASE_SENSITIVE, caseSensitive());
	s.setValue(_settingsRootCategory + SETTINGS_REGEX, regex());
	s.setValue(_settingsRootCategory + SETTINGS_WHOLE_WORDS, wholeWords());
}
