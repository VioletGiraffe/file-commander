#pragma once

#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "cfilesystemobject.h"

#include "filelisttestitems.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QAbstractItemModelTester>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>

// Settable via the --std-seed command-line option; see main.cpp
extern uint32_t g_randomSeed;

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
