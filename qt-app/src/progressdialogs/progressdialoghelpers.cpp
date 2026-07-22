#include "progressdialoghelpers.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QObject>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

QString secondsToTimeIntervalString(uint32_t secondsTotal)
{
	const auto hours = secondsTotal / 3600;
	const auto minutes = (secondsTotal - (hours * 3600)) / 60;
	const auto seconds = secondsTotal % 60;

	QString result;
	if (hours > 0)
		result = QString::number(hours) % ' ' % QObject::tr("hours");

	if (minutes > 0)
	{
		if (hours > 0)
			result += ' ';
		result += QString::number(minutes) % ' ' % QObject::tr("minutes");
	}

	if (minutes > 0 || hours > 0)
		result += ' ';

	result += QString::number(seconds) % ' ' % QObject::tr("seconds");

	return result;
}

QString fileOperationEntryKindNoun(const OperationEntryKind kind)
{
	using enum OperationEntryKind;
	switch (kind)
	{
	case RegularFile: return QObject::tr("file");
	case Directory: return QObject::tr("folder");
	case FileLink: return QObject::tr("link to a file");
	case DirectoryLink: return QObject::tr("link to a folder");
	case Other: return QObject::tr("special entry");
	}
	return {};
}

QString fileOperationFailedActionText(const FailedAction action)
{
	using enum FailedAction;
	switch (action)
	{
	case InspectSource: return QObject::tr("Reading the source entry's properties");
	case InspectDestination: return QObject::tr("Reading the destination entry's properties");
	case ReadSource: return QObject::tr("Reading the source file");
	case CreateDestinationDirectory: return QObject::tr("Creating the destination folder");
	case PrepareStagingFile: return QObject::tr("Creating the temporary destination file");
	case WriteDestination: return QObject::tr("Writing the destination file");
	case PreserveFileMetadata: return QObject::tr("Preserving the file's attributes");
	case PublishDestination: return QObject::tr("Finalizing the destination file");
	case RenameEntry: return QObject::tr("Moving the entry into place");
	case MakeWritable: return QObject::tr("Making the entry writable");
	case RemoveEntry: return QObject::tr("Removing the entry");
	case RemovePublishedMoveSource: return QObject::tr("Removing the moved entry's source");
	case CleanupStaging: return QObject::tr("Removing the temporary destination file");
	case PreserveDirectoryTimestamps: return QObject::tr("Preserving the folder's timestamps");
	}
	return {};
}

QString fileSystemErrorText(const CFileSystemError& error)
{
	using enum FileErrorCategory;
	QString reason;
	switch (error.category)
	{
	case NotFound: reason = QObject::tr("the entry does not exist"); break;
	case AlreadyExists: reason = QObject::tr("an entry with this name already exists"); break;
	case CrossDevice: reason = QObject::tr("the destination is on a different volume"); break;
	case ReadOnly: reason = QObject::tr("the entry is read-only"); break;
	case PermissionDenied: reason = QObject::tr("access denied"); break;
	case NotEnoughSpace: reason = QObject::tr("not enough free space"); break;
	case Unsupported: reason = QObject::tr("the operation is not supported here"); break;
	case IoFailure: reason = QObject::tr("an input/output error occurred"); break;
	}

	if (!error.diagnostic.isEmpty())
		reason += QLatin1String(" (") % error.diagnostic % QLatin1Char(')');
	else if (error.nativeCode != 0)
		reason += QObject::tr(" (error code %1)").arg(error.nativeCode);
	return reason;
}

QString fileOperationDiagnosticText(const OperationDiagnostic& diagnostic)
{
	return QDir::toNativeSeparators(diagnostic.source.path.value()) % QLatin1String(": ")
		% fileOperationFailedActionText(diagnostic.failure.action) % QLatin1String(" - ")
		% fileSystemErrorText(diagnostic.failure.filesystemError);
}
