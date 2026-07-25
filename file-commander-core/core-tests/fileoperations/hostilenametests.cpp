// Names that are legal but awkward: the engine has to carry each one through a copy, a move and a delete without
// mangling, skipping or losing it. Filenames are the engine's least controlled input, and every path here runs through
// UTF-16/UTF-8 conversion, extension splitting and native path assembly.
//
// Non-ASCII names are assembled from code points rather than written as glyphs, so that the test does not depend on the
// compiler agreeing about the source file's encoding - and, for the two normalization forms, because as literal text
// they are indistinguishable on screen and a tool could silently renormalize one into the other.
//
// Every case skips itself where the filesystem will not store the name verbatim, which is what keeps the table free of
// platform guards: Windows strips trailing dots and spaces and rejects `:*?`, macOS renormalizes Unicode.

#include "fileoperations/ctransferexecutor.h"
#include "fileoperations/cdeleteexecutor.h"
#include "fileoperations/coperationexecutioncontext.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

namespace
{

OperationSummary runTransfer(OperationScript& script, const TransferKind kind, const QStringList& sources,
	const DestinationIntent intent, const QString& destination)
{
	const auto request = makeTransferRequest(kind, sources, intent, destination);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, 64 * 1024 };
	return executor.run(*request);
}

OperationSummary runDelete(OperationScript& script, const QStringList& sources)
{
	const auto request = makePermanentDeleteRequest(sources);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Items);
	CDeleteExecutor executor{ context };
	return executor.run(*request);
}

QString nonBmpName()
{
	const char32_t rocket = 0x1F680; // One character, two UTF-16 code units - the case that catches truncated conversions
	return QString::fromUcs4(&rocket, 1) % QStringLiteral("launch.bin");
}

QStringList awkwardNames()
{
	return {
		QStringLiteral("spaces in the middle.bin"),
		QStringLiteral(".leading-dot.bin"),  // Hidden on POSIX: enumeration must not quietly drop it
		QStringLiteral("archive.tar.gz"),    // Two suffixes, for the base-name/extension split
		QStringLiteral("no-extension"),
		QStringLiteral("caf") % QChar{ 0x00E9 } % QStringLiteral(".bin"),   // Precomposed U+00E9
		QStringLiteral("cafe") % QChar{ 0x0301 } % QStringLiteral(".bin"),  // "e" + combining acute: the same glyph, decomposed
		QString{ QChar{ 0x6587 } } % QChar{ 0x4EF6 } % QStringLiteral(".bin"), // CJK, still within the BMP
		nonBmpName(),
		QString(200, QChar{ 'x' }) % QStringLiteral(".bin"), // Long single component, comfortably under the 255 limit
		// Looks exactly like the staging file a copy creates. Nothing may mistake a real source entry for its own debris.
		QStringLiteral(".file-commander-copy-decoy.bin"),
		QStringLiteral("trailing-space "), // Windows strips it, and so skips this case
		QStringLiteral("trailing-dot."),
		QStringLiteral("colon:star*question?.bin"),
		QStringLiteral("back\\slash.bin"),                                // A separator on Windows, an ordinary character on POSIX
		QStringLiteral("line") % QChar{ '\n' } % QStringLiteral("break.bin")
	};
}

// Whether the name can be tested here at all: the filesystem has to accept it and hand back the very same name, since
// a name it silently rewrites cannot demonstrate anything about the engine.
bool filesystemStoresNameVerbatim(const QString& directory, const QString& name)
{
	const QString path = directory % '/' % name;
	{
		QFile probe{ path };
		if (!probe.open(QFile::WriteOnly))
			return false;
	}

	const bool storedVerbatim = QDir{ directory }.entryList(QDir::Files | QDir::Hidden | QDir::System).contains(name);
	QFile::remove(path);
	return storedVerbatim;
}

void checkNameSurvivesCopyMoveDelete(const QString& name)
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	if (!filesystemStoresNameVerbatim(base, name))
	{
		WARN("Name not stored verbatim by this filesystem, case skipped");
		return;
	}

	// The name is exercised twice over: as a leaf, and as a directory component of a longer path.
	REQUIRE(QDir{}.mkpath(base % "/src/asFile"));
	REQUIRE(QDir{}.mkpath(base % "/src/asDir/" % name));
	writeTestFile(base % "/src/asFile/" % name, patternedContents(300));
	writeTestFile(base % "/src/asDir/" % name % "/inner.bin", patternedContents(500));

	OperationScript script;

	REQUIRE(QDir{}.mkpath(base % "/dest"));
	const auto copySummary = runTransfer(script, TransferKind::Copy, { base % "/src" }, DestinationIntent::IntoDirectory,
		base % "/dest");
	CHECK(copySummary.status == CompletionStatus::Completed);
	CHECK(script.seenRequests.empty()); // Nothing about an awkward name should need a decision from the user
	requireEqualTrees(base % "/src", base % "/dest/src");

	REQUIRE(QDir{}.mkpath(base % "/moved"));
	const auto moveSummary = runTransfer(script, TransferKind::Move, { base % "/src" }, DestinationIntent::IntoDirectory,
		base % "/moved");
	CHECK(moveSummary.status == CompletionStatus::Completed);
	CHECK(script.seenRequests.empty());
	CHECK(entryAbsent(base % "/src"));
	requireEqualTrees(base % "/dest/src", base % "/moved/src"); // Against the copy, which the assertion above vouched for

	const auto deleteSummary = runDelete(script, { base % "/moved/src" });
	CHECK(deleteSummary.status == CompletionStatus::Completed);
	CHECK(script.seenRequests.empty());
	CHECK(entryAbsent(base % "/moved/src"));
}

} // namespace

TEST_CASE("hostile names: awkward but legal names survive copy, move and delete", "[hostilenames]")
{
	for (const QString& name : awkwardNames())
	{
		INFO("Name under test: " << name.toStdString());
		checkNameSurvivesCopyMoveDelete(name);
	}
}
