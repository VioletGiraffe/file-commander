#ifndef CFINDDIALOG_H
#define CFINDDIALOG_H

#include <QDialog>

namespace Ui {
class CFindDialog;
}

class CFindDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CFindDialog(QWidget *parent = 0);
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

private:
	void saveSearchSettings() const;

private:
	Ui::CFindDialog *ui;
};

#endif // CFINDDIALOG_H
