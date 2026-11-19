#include "cfilecomparator.h"

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QObject>
RESTORE_COMPILER_WARNINGS

#include <initializer_list>
#include <utility> // std::move

std::expected<FilesToCompare, UserMessage> filesToCompare(const CFileSystemObject& current, const CFileSystemObject& otherCurrent, const QString& otherFolder)
{
	const QString title = QObject::tr("File comparison");
	if (!current.isFile())
		return std::unexpected{ UserMessage{ MessageSeverity::Warning, title, QObject::tr("The selected item is not a file:\n%1").arg(toNativeSeparators(current.fullAbsolutePath())) } };

	FilesToCompare files{
		.pathA = current.fullAbsolutePath(),
		.pathB = otherCurrent.isFile() ? otherCurrent.fullAbsolutePath() : QDir{ otherFolder }.filePath(current.fullName().toString()),
	};

	const QFileInfo infoA{ files.pathA }, infoB{ files.pathB };
	for (const QFileInfo& info : { infoA, infoB })
	{
		if (!info.exists())
		{
			return std::unexpected{ UserMessage{ MessageSeverity::Warning, title,
				QObject::tr("The file\n%1\nis selected for comparison, but doesn't exist.").arg(toNativeSeparators(info.filePath())) } };
		}
	}

	if (infoA.size() != infoB.size())
	{
		return std::unexpected{ UserMessage{ MessageSeverity::Warning, title, QObject::tr("Files have different sizes:\n%1: %2\n%3: %4")
			.arg(toNativeSeparators(files.pathA)).arg(infoA.size()).arg(toNativeSeparators(files.pathB)).arg(infoB.size()) } };
	}

	return files;
}

UserMessage fileComparisonResultMessage(const ContentComparisonResult result, const FilesToCompare& files)
{
	const QString a = toNativeSeparators(files.pathA), b = toNativeSeparators(files.pathB);
	switch (result)
	{
	case ContentComparisonResult::Equal:
		return { MessageSeverity::Information, QObject::tr("Files are identical"), QObject::tr("The file\n%1\n\nis IDENTICAL to\n\n%2.").arg(a, b) };
	case ContentComparisonResult::Different:
		return { MessageSeverity::Warning, QObject::tr("Files differ"), QObject::tr("The file\n%1\n\nis DIFFERENT from\n\n%2.").arg(a, b) };
	case ContentComparisonResult::Error:
		return { MessageSeverity::Error, QObject::tr("Comparison failed"), QObject::tr("Failed to read the files:\n%1\n\n%2").arg(a, b) };
	case ContentComparisonResult::Aborted:
		break;
	}

	assert_and_return_unconditional_r("An aborted comparison has no result to show", {});
}

CFileComparator::CFileComparator(QString pathA, QString pathB)
{
	_task.start([this, pathA{ std::move(pathA) }, pathB{ std::move(pathB) }](const std::atomic<bool>& cancelled) -> std::optional<ContentComparisonResult> {
		const auto totalBytes = static_cast<uint64_t>(QFileInfo{ pathA }.size());
		uint64_t comparedBytes = 0;
		const auto result = compareFileContents(pathA, pathB, cancelled, [&](uint64_t bytesCompared) {
			comparedBytes += bytesCompared;
			_percent = static_cast<int>(comparedBytes * 100 / totalBytes); // totalBytes cannot be 0 here: no bytes are reported for an empty file
		});

		if (result == ContentComparisonResult::Aborted)
			return std::nullopt;
		return result;
	});
}
