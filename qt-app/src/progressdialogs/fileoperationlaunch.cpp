#include "fileoperationlaunch.h"

// Submodule includes
#include "directoryscan/directoryscanner.h"
#include "directoryscan/thinioqstring.h"
#include "fileoperations/centrypath.h"
#include "paths/pathsyntax.h"


#include <optional>

namespace
{

bool hasTrailingPathSeparator(const QString& path)
{
	if (path.endsWith(QLatin1Char('/')))
		return true;
#ifdef _WIN32
	return path.endsWith(QLatin1Char('\\'));
#else
	return false;
#endif
}

// Links followed. Empty when there is no readable entry, and for a path that is not absolute.
std::optional<thin_io::entry_kind> followedEntryKind(const QString& path)
{
	if (absoluteRootLength(path) == 0)
		return {};

	const auto entry = getDirectoryEntry(withoutTrailingSeparator(path));
	if (!entry)
		return {};

	return followedStatus(*entry).attributes.kind;
}

bool isFile(const QString& path)
{
	return followedEntryKind(path) == thin_io::entry_kind::regular_file;
}

} // namespace

DestinationIntent transferDestinationIntent(const QStringList& rawSourcePaths, const QString& destinationText)
{
	using enum DestinationIntent;

	if (rawSourcePaths.size() != 1)
		return IntoDirectory;
	if (hasTrailingPathSeparator(destinationText))
		return IntoDirectory;

	// A single source that is not a file, links followed, always maps into the destination.
	if (!isFile(rawSourcePaths.front()))
		return IntoDirectory;

	// A single file: the edited destination is the exact target unless it already names a directory.
	return followedEntryKind(destinationText) == thin_io::entry_kind::directory ? IntoDirectory : ExactEntry;
}

QString prefillTransferDestination(const TransferKind kind, const QStringList& rawSourcePaths, const QString& destinationDirectory)
{
	if (kind != TransferKind::Copy || rawSourcePaths.size() != 1 || !isFile(rawSourcePaths.front()))
		return destinationDirectory;

	const auto source = parseOperationPath(rawSourcePaths.front());
	const auto destination = parseOperationPath(destinationDirectory);
	if (!source || !destination)
		return destinationDirectory;

	return destination->child(source->name()).value();
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
