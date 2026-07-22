#include "fileoperationlaunch.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

DestinationIntent transferDestinationIntent(const QStringList& rawSourcePaths, const QString& destinationText)
{
	using enum DestinationIntent;

	if (rawSourcePaths.size() != 1)
		return IntoDirectory;

	// A single non-file source (a directory, a link, a special entry) always maps into the destination.
	if (!QFileInfo{ rawSourcePaths.front() }.isFile())
		return IntoDirectory;

	// A single file: the edited destination is the exact target unless it already names a directory.
	return QFileInfo{ destinationText }.isDir() ? IntoDirectory : ExactEntry;
}

QString prefillTransferDestination(const TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationDirectory)
{
	if (kind == TransferKind::Copy && rawSourcePaths.size() == 1 && QFileInfo{ rawSourcePaths.front() }.isFile())
		return QDir{ destinationDirectory }.filePath(QFileInfo{ rawSourcePaths.front() }.fileName());

	return destinationDirectory;
}

std::expected<TransferRequest, RequestValidationError> makeUiTransferRequest(
	const TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationText)
{
	const DestinationIntent intent = transferDestinationIntent(rawSourcePaths, destinationText);
	return makeTransferRequest(kind, rawSourcePaths, intent, destinationText);
}

DeletionBackend deletionBackendFor(const bool toTrash)
{
	if (toTrash)
	{
#if defined _WIN32 || defined __APPLE__
		return DeletionBackend::NativeTrash;
#else
		return DeletionBackend::InternalJob; // No native Linux trash yet; unifying that is a later product change
#endif
	}

#ifdef _WIN32
	return DeletionBackend::NativeShellPermanent;
#else
	return DeletionBackend::InternalJob;
#endif
}
