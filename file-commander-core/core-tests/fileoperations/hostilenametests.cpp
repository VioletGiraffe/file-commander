// Names that are legal but awkward: the engine has to carry each one through a copy, a move and a delete without
// mangling, skipping or losing it. Filenames are the engine's least controlled input, and every path here runs through
// UTF-16/UTF-8 conversion, extension splitting and native path assembly.
//
// Non-ASCII names are assembled from code points rather than written as glyphs, so that the test does not depend on the
// compiler agreeing about the source file's encoding - and, for the two normalization forms, because as literal text
// they are indistinguishable on screen and a tool could silently renormalize one into the other.
//
// Every case skips itself where the filesystem will not store the name verbatim, which is what keeps the table free of
// platform guards: Windows rejects `:*?`, macOS renormalizes Unicode.

#include "fileoperations/coperationexecutioncontext.h"

#include "fileoperationtesthelpers.h"

#include "3rdparty/magic_enum/magic_enum.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

namespace
{

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
		// Win32 path normalization strips a trailing space or period, but Qt's file APIs go out `\\?\`-prefixed and so
		// bypass it: where NTFS does end up holding the name, the engine has to reach it rather than the case skipping.
		QStringLiteral("trailing-space "),
		QStringLiteral("trailing-dot."),
		QStringLiteral("colon:star*question?.bin"),
		QStringLiteral("back\\slash.bin"),                                // A separator on Windows, an ordinary character on POSIX
		QStringLiteral("line") % QChar{ '\n' } % QStringLiteral("break.bin")
	};
}

// Reports what the engine asked about, which the harness's own "nextDecision < decisions.size()" assert cannot: that
// one only says a question arrived, never which. Non-fatal on purpose, so that one run covers every name in the table.
[[nodiscard]] bool noDecisionRequested(const OperationScript& script, const char* stage)
{
	if (script.seenRequests.empty())
		return true;

	std::string report = std::string{ "The engine needed a policy decision during " } + stage
		+ " - carrying an awkward name should not raise a question.";
	for (const DecisionRequest& request : script.seenRequests)
	{
		report += "\n  " + std::string{ magic_enum::enum_name(request.issue.kind) } + " on "
			+ request.issue.source.path.value().toStdString();
		if (request.issue.failure)
			report += "\n    failed action: " + std::string{ magic_enum::enum_name(request.issue.failure->action) }
				+ ", error: " + std::string{ magic_enum::enum_name(request.issue.failure->filesystemError.category) }
				+ " native=" + std::to_string(request.issue.failure->filesystemError.nativeCode)
				+ " (" + request.issue.failure->filesystemError.diagnostic.toStdString() + ')';
	}

	FAIL_CHECK(report);
	return false;
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
	// Cancel rather than answer anything that does come up: it ends the run deterministically instead of tripping the
	// harness's out-of-decisions assert, leaving the check below free to report the actual question.
	script.cancelInsteadOfAnswering = true;

	REQUIRE(QDir{}.mkpath(base % "/dest"));
	const auto copySummary = runTransfer(script, TransferKind::Copy, { base % "/src" }, DestinationIntent::IntoDirectory,
		base % "/dest");
	// Once a decision was requested the run was cancelled, so everything below would only pile noise onto the report.
	if (!noDecisionRequested(script, "the copy"))
		return;

	CHECK(copySummary.status == CompletionStatus::Completed);
	requireEqualTrees(base % "/src", base % "/dest/src");

	REQUIRE(QDir{}.mkpath(base % "/moved"));
	const auto moveSummary = runTransfer(script, TransferKind::Move, { base % "/src" }, DestinationIntent::IntoDirectory,
		base % "/moved");
	if (!noDecisionRequested(script, "the move"))
		return;

	CHECK(moveSummary.status == CompletionStatus::Completed);
	CHECK(entryAbsent(base % "/src"));
	requireEqualTrees(base % "/dest/src", base % "/moved/src"); // Against the copy, which the assertion above vouched for

	const auto deleteSummary = runDelete(script, { base % "/moved/src" });
	if (!noDecisionRequested(script, "the delete"))
		return;

	CHECK(deleteSummary.status == CompletionStatus::Completed);
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
