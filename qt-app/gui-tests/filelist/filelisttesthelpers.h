#pragma once

#include "panel/filelistwidget/model/cfilelistmodel.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractItemModelTester>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <time.h>

inline FileListRow makeRow(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	FileListRow row;
	row.type = type;
	row.name = name;
	row.extension = extension;
	row.fullName = extension.isEmpty() ? name : name + '.' + extension;
	row.fullPath = QStringLiteral("/folder/") + row.fullName + (type == Directory ? QStringLiteral("/") : QString{});
	row.hash = pathHash(row.fullPath);
	row.size = size;
	row.modificationTime = modificationTime;
	return row;
}

inline FileListRow makeCdUpRow()
{
	FileListRow row;
	row.type = Directory;
	row.fullName = QStringLiteral("..");
	row.fullPath = QStringLiteral("/");
	row.hash = pathHash(row.fullPath);
	row.isCdUp = true;
	return row;
}

inline QStringList displayedNames(const CFileListModel& model)
{
	QStringList names;
	for (int row = 0; row < model.rowCount(); ++row)
		names.push_back(model.rowAt(row).fullName);
	return names;
}

// Checks every change against Qt's model contract; a violation aborts the run
struct TestedModel
{
	CFileListModel model{ nullptr };
	QAbstractItemModelTester tester{ &model, QAbstractItemModelTester::FailureReportingMode::Fatal };
};
