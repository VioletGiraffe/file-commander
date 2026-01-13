#pragma once

#include <QMainWindow>

struct FileStatistics;
class QTreeWidget;
class CController;

class CFileStatsWindow final : public QMainWindow
{
public:
	CFileStatsWindow(QWidget* parent, const FileStatistics& stats, CController& controller);

private:
	void fillFileList(const FileStatistics& stats);

private:
	QTreeWidget* _list = nullptr;
};
