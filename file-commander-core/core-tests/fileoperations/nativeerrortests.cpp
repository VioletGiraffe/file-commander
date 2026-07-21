#include "operationperformertesthelpers.h"
#include "cfilemanipulator.h"

// test_utils
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
TEST_CASE((std::string("Directory rename failure has a native diagnostic #") + std::to_string(rand())).c_str(), "[filemanipulator-errors]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	REQUIRE(sourceDirectory.isValid());

	const QString childDirectoryPath = sourceDirectory.path() % "/child";
	REQUIRE(QDir{}.mkpath(childDirectoryPath));

	const CFileSystemObject sourceObject(sourceDirectory.path());
	CFileManipulator manipulator(sourceObject);
	REQUIRE(manipulator.moveAtomically(childDirectoryPath, QStringLiteral("moved")) == FileOperationResultCode::Fail);
	REQUIRE(!manipulator.lastErrorMessage().isEmpty());
	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
}
#else
TEST_CASE((std::string("Directory removal failure has a native diagnostic #") + std::to_string(rand())).c_str(), "[filemanipulator-errors]")
{
	QTemporaryDir directory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_XXXXXX");
	REQUIRE(directory.isValid());

	const QString nativePath = QDir::toNativeSeparators(directory.path());
	const HANDLE directoryHandle = ::CreateFileW(reinterpret_cast<const WCHAR*>(nativePath.utf16()), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	REQUIRE(directoryHandle != INVALID_HANDLE_VALUE);

	const CFileSystemObject directoryObject(directory.path());
	CFileManipulator manipulator(directoryObject);
	const auto result = manipulator.remove();
	const QString errorMessage = manipulator.lastErrorMessage();
	::CloseHandle(directoryHandle);

	REQUIRE(result == FileOperationResultCode::Fail);
	REQUIRE(!errorMessage.isEmpty());
	REQUIRE(QFileInfo::exists(directory.path()));
}
#endif
