#ifndef CFILEOPERATIONCONFIRMATIONPROMPT_H
#define CFILEOPERATIONCONFIRMATIONPROMPT_H

#include <QDialog>

namespace Ui {
class CFileOperationConfirmationPrompt;
}

class CFileOperationConfirmationPrompt : public QDialog
{
	Q_OBJECT

public:
	explicit CFileOperationConfirmationPrompt(const QString & caption, const QString& labelText, const QString& editText, QWidget *parent = 0);
	~CFileOperationConfirmationPrompt();

	QString text() const;

private:
	Ui::CFileOperationConfirmationPrompt *ui;
};

#endif // CFILEOPERATIONCONFIRMATIONPROMPT_H
