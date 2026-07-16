#pragma once

#include <QMainWindow>

#include <cstdint>

class CController;
struct FileStatistics;

class QLabel;
class QTreeWidget;

class CFileStatsWindow final : public QMainWindow
{
public:
	CFileStatsWindow(QWidget* parent, const FileStatistics& stats, CController& controller, uint64_t elapsedMs);

private:
	void fillFileList(const FileStatistics& stats);

private:
	QTreeWidget* _list = nullptr;
	QLabel* _statusLabel = nullptr;
};
