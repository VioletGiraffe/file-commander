#pragma once

#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractItemModelTester>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <time.h>

// An entry of /folder/; a test that changes it before listing keeps the properties, the others use makeRow()
inline CFileSystemObjectProperties rowProperties(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	CFileSystemObjectProperties properties;
	properties.type = type;
	const QString fullName = extension.isEmpty() ? name : name + '.' + extension;
	properties.fullPath = QStringLiteral("/folder/") + fullName + (type == Directory ? QStringLiteral("/") : QString{});
	properties.size = size;
	properties.modificationTime = modificationTime;
	return properties;
}

inline CFileSystemObject makeRow(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	return CFileSystemObject{ rowProperties(type, name, extension, size, modificationTime) };
}

inline CFileSystemObjectProperties cdUpRowProperties()
{
	CFileSystemObjectProperties properties;
	properties.type = Directory;
	properties.fullPath = QStringLiteral("/");
	properties.isCdUp = true;
	return properties;
}

inline CFileSystemObject makeCdUpRow()
{
	return CFileSystemObject{ cdUpRowProperties() };
}

inline QStringList displayedNames(const CFileListModel& model)
{
	QStringList names;
	for (int row = 0; row < model.rowCount(); ++row)
		names.push_back(model.rowAt(row).fullName().toString());
	return names;
}

// Checks every change against Qt's model contract; a violation aborts the run
struct TestedModel
{
	CFileListModel model{ nullptr };
	QAbstractItemModelTester tester{ &model, QAbstractItemModelTester::FailureReportingMode::Fatal };
};
