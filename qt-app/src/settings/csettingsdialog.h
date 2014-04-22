#ifndef CSETTINGSDIALOG_H
#define CSETTINGSDIALOG_H

#include <QDialog>
#include <vector>

namespace Ui {
class CSettingsDialog;
}

class QListWidgetItem;

class CSettingsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CSettingsDialog(QWidget *parent = 0);
	~CSettingsDialog();

	void addSettingsPage(QWidget * page, const QString& pageName = QString());

signals:
	void settingsChanged();

private slots:
	void pageChanged(QListWidgetItem *item);
	virtual void accept() override;

private:
	Ui::CSettingsDialog *ui;
};

#endif // CSETTINGSDIALOG_H
