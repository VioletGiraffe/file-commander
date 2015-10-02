#pragma once

#include "compiler/compiler_warnings_control.h"
#include "filesearchengine/cfilesearchengine.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFilesSearchWindow;
}

class CFilesSearchWindow : public QMainWindow, public CFileSearchEngine::FileSearchListener
{
	Q_OBJECT

public:
	explicit CFilesSearchWindow(QWidget *parent, const QString& root);
	~CFilesSearchWindow();

	void itemScanned(const QString& currentItem) const override;
	void matchFound(const QString& path) const override;
	void searchFinished() const override;

private:
	void search();

private:
	Ui::CFilesSearchWindow *ui;

	CFileSearchEngine& _engine;
};

