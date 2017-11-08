#pragma once

#include "compiler/compiler_warnings_control.h"
#include "widgets/cpersistentwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFindDialog;
}

class CFindDialog : public QDialog
{
	Q_OBJECT

public:
	CFindDialog(QWidget *parent, const QString& settingsRootCategory = QString());
	~CFindDialog();

	QString searchExpression() const;
	bool regex() const;
	bool searchBackwards() const;
	bool wholeWords() const;
	bool caseSensitive() const;

signals:
	void find();
	void findNext();

public slots:
	void accept() override;

protected:
	void showEvent(QShowEvent * e) override;
	void closeEvent(QCloseEvent * e) override;

private:
	void saveSearchSettings() const;

private:
	QString _settingsRootCategory;
	Ui::CFindDialog *ui;
};
