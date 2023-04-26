#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFindDialog;
}

class CFindDialog final: public QDialog
{
	Q_OBJECT

public:
	CFindDialog(QWidget *parent, QString settingsRootCategory = {});
	~CFindDialog() override;

	[[nodiscard]]QString searchExpression() const;
	[[nodiscard]] bool regex() const;
	[[nodiscard]] bool searchBackwards() const;
	[[nodiscard]] bool wholeWords() const;
	[[nodiscard]] bool caseSensitive() const;

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
