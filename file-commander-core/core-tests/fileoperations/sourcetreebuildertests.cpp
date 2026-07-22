// WP4: the immutable hierarchical source manifest - shapes and totals, operation-specific link handling,
// ownership, cycle termination, scanning progress, and cancellation.

#include "fileoperations/csourcetreebuilder.h"
#include "fileoperations/coperationexecutioncontext.h"

#include "fileoperationtesthelpers.h"
#include "lang/utils.hpp" // mv()

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{

struct ScanScript
{
	std::vector<ProgressSnapshot> progress;
	int checkpointsBeforeCancel = -1; // -1 = never cancel
	int checkpointCalls = 0;
};

COperationExecutionContext scanContext(ScanScript& script)
{
	return COperationExecutionContext{
		PrimaryProgressUnit::Bytes,
		[&script] {
			++script.checkpointCalls;
			return script.checkpointsBeforeCancel < 0 || script.checkpointCalls <= script.checkpointsBeforeCancel;
		},
		[](const DecisionRequest&) -> std::optional<Decision> {
			FAIL("Scanning must never request a decision");
			return {};
		},
		[&script](const ProgressSnapshot& snapshot) { script.progress.push_back(snapshot); }
	};
}

SourceNode buildTree(const QString& rootPath, const SourceTreeBuildMode mode)
{
	ScanScript script;
	auto context = scanContext(script);
	auto result = buildSourceTree(context, snapshotOf(rootPath), mode);
	REQUIRE(std::holds_alternative<SourceNode>(result));
	return std::get<SourceNode>(mv(result));
}

const SourceNode* childNamed(const SourceNode& node, const QString& name)
{
	for (const SourceNode& child : node.children)
	{
		if (child.entry.path.name() == name)
			return &child;
	}
	return nullptr;
}

} // namespace

TEST_CASE("source tree: shape and subtree totals", "[sourcetree]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root/sub/empty"));
	writeTestFile(base % "/root/a.bin", QByteArray(1000, 'a'));
	writeTestFile(base % "/root/sub/b.bin", QByteArray(2000, 'b'));
	writeTestFile(base % "/root/sub/c.bin", QByteArray(3000, 'c'));

	const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);

	CHECK(tree.entry.kind == OperationEntryKind::Directory);
	CHECK(tree.ownership == SourceOwnership::Owned);
	CHECK(tree.subtreeItems == 6); // root, a.bin, sub, b.bin, c.bin, empty
	CHECK(tree.subtreeBytes == 6000);
	REQUIRE(tree.children.size() == 2);

	const SourceNode* file = childNamed(tree, QStringLiteral("a.bin"));
	REQUIRE(file != nullptr);
	CHECK(file->entry.kind == OperationEntryKind::RegularFile);
	CHECK(file->entry.size == 1000);
	CHECK(file->subtreeItems == 1);
	CHECK(file->subtreeBytes == 1000);
	CHECK(file->children.empty());

	const SourceNode* sub = childNamed(tree, QStringLiteral("sub"));
	REQUIRE(sub != nullptr);
	CHECK(sub->subtreeItems == 4); // sub, b.bin, c.bin, empty
	CHECK(sub->subtreeBytes == 5000);

	const SourceNode* empty = childNamed(*sub, QStringLiteral("empty"));
	REQUIRE(empty != nullptr);
	CHECK(empty->entry.kind == OperationEntryKind::Directory);
	CHECK(empty->subtreeItems == 1);
	CHECK(empty->subtreeBytes == 0);
	CHECK(empty->children.empty());
}

TEST_CASE("source tree: empty, deep, and wide directories", "[sourcetree]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("an empty root is a single node")
	{
		REQUIRE(QDir{}.mkpath(base % "/empty"));
		const SourceNode tree = buildTree(base % "/empty", SourceTreeBuildMode::MaterializingTransfer);
		CHECK(tree.subtreeItems == 1);
		CHECK(tree.subtreeBytes == 0);
		CHECK(tree.children.empty());
	}

	SECTION("a deep chain")
	{
		constexpr int depth = 20; // Modest: the nested path must stay under Windows' MAX_PATH on top of the temp-dir prefix
		QString path = base % "/deep";
		for (int i = 0; i < depth; ++i)
			path += QStringLiteral("/level");
		REQUIRE(QDir{}.mkpath(path));
		writeTestFile(path % "/leaf.bin", QByteArray(10, 'x'));

		const SourceNode tree = buildTree(base % "/deep", SourceTreeBuildMode::MaterializingTransfer);
		CHECK(tree.subtreeItems == static_cast<size_t>(depth) + 2); // The chain, its root, and the leaf file
		CHECK(tree.subtreeBytes == 10);
	}

	SECTION("a wide directory")
	{
		constexpr int fileCount = 64;
		REQUIRE(QDir{}.mkpath(base % "/wide"));
		for (int i = 0; i < fileCount; ++i)
			writeTestFile(base % "/wide/file" % QString::number(i) % ".bin", QByteArray(10, 'x'));

		const SourceNode tree = buildTree(base % "/wide", SourceTreeBuildMode::MaterializingTransfer);
		CHECK(tree.subtreeItems == fileCount + 1);
		CHECK(tree.subtreeBytes == fileCount * 10);
		CHECK(tree.children.size() == fileCount);
	}
}

TEST_CASE("source tree: directory links per build mode", "[sourcetree][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root"));
	REQUIRE(QDir{}.mkpath(base % "/target/nested"));
	writeTestFile(base % "/target/t.bin", QByteArray(4000, 't'));
	writeTestFile(base % "/target/nested/n.bin", QByteArray(500, 'n'));
	REQUIRE(createDirectoryLink(base % "/target", base % "/root/dirlink"));

	SECTION("permanent delete never follows: the link is a leaf")
	{
		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::PermanentDelete);
		CHECK(tree.subtreeItems == 2); // root and the link entry itself
		CHECK(tree.subtreeBytes == 0);

		const SourceNode* link = childNamed(tree, QStringLiteral("dirlink"));
		REQUIRE(link != nullptr);
		CHECK(link->entry.kind == OperationEntryKind::DirectoryLink);
		CHECK(link->children.empty());
		CHECK(link->ownership == SourceOwnership::Owned);
	}

	SECTION("materializing transfer borrows the linked content")
	{
		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);
		CHECK(tree.subtreeItems == 6); // root, dirlink, t.bin, nested, n.bin... and root has just the link child
		CHECK(tree.subtreeBytes == 4500);

		const SourceNode* link = childNamed(tree, QStringLiteral("dirlink"));
		REQUIRE(link != nullptr);
		CHECK(link->entry.kind == OperationEntryKind::DirectoryLink);
		CHECK(link->ownership == SourceOwnership::Owned); // The link entry itself is ours
		CHECK(link->subtreeItems == 5);
		CHECK(link->subtreeBytes == 4500);

		const SourceNode* borrowedFile = childNamed(*link, QStringLiteral("t.bin"));
		REQUIRE(borrowedFile != nullptr);
		CHECK(borrowedFile->ownership == SourceOwnership::BorrowedThroughDirectoryLink);
		CHECK(borrowedFile->entry.size == 4000);

		const SourceNode* borrowedDir = childNamed(*link, QStringLiteral("nested"));
		REQUIRE(borrowedDir != nullptr);
		CHECK(borrowedDir->ownership == SourceOwnership::BorrowedThroughDirectoryLink);
		REQUIRE(borrowedDir->children.size() == 1);
		CHECK(borrowedDir->children.front().ownership == SourceOwnership::BorrowedThroughDirectoryLink);
	}
}

#ifndef _WIN32
TEST_CASE("source tree: file links per build mode", "[sourcetree][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root"));
	writeTestFile(base % "/target.bin", QByteArray(7000, 't'));
	REQUIRE(QFile::link(base % "/target.bin", base % "/root/filelink.bin"));

	SECTION("materializing transfer carries the followed target size")
	{
		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);
		const SourceNode* link = childNamed(tree, QStringLiteral("filelink.bin"));
		REQUIRE(link != nullptr);
		CHECK(link->entry.kind == OperationEntryKind::FileLink);
		CHECK(link->entry.size == 7000);
		CHECK(tree.subtreeBytes == 7000);
	}

	SECTION("permanent delete sees only the link entry")
	{
		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::PermanentDelete);
		const SourceNode* link = childNamed(tree, QStringLiteral("filelink.bin"));
		REQUIRE(link != nullptr);
		CHECK(link->entry.kind == OperationEntryKind::FileLink);
		CHECK(link->entry.size == 0);
		CHECK(tree.subtreeBytes == 0);
	}
}
#endif

TEST_CASE("source tree: broken links remain leaf entries", "[sourcetree][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root"));
	REQUIRE(QDir{}.mkpath(base % "/target"));
	REQUIRE(createDirectoryLink(base % "/target", base % "/root/brokenlink"));
	REQUIRE(QDir{}.rmdir(base % "/target"));

	const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);
	CHECK(tree.subtreeItems == 2);

	const SourceNode* link = childNamed(tree, QStringLiteral("brokenlink"));
	REQUIRE(link != nullptr);
#ifdef _WIN32
	CHECK(link->entry.kind == OperationEntryKind::DirectoryLink); // A broken junction stays a directory entry
#else
	CHECK(link->entry.kind == OperationEntryKind::FileLink); // A POSIX symlink entry carries no target type
#endif
	CHECK(link->children.empty());
	CHECK(link->entry.size == 0);
}

TEST_CASE("source tree: link cycles terminate by identity", "[sourcetree][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("a link to its own ancestor becomes a leaf")
	{
		REQUIRE(QDir{}.mkpath(base % "/root/sub"));
		writeTestFile(base % "/root/a.bin", QByteArray(100, 'a'));
		REQUIRE(createDirectoryLink(base % "/root", base % "/root/sub/uplink"));

		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);
		CHECK(tree.subtreeItems == 4); // root, a.bin, sub, uplink

		const SourceNode* sub = childNamed(tree, QStringLiteral("sub"));
		REQUIRE(sub != nullptr);
		const SourceNode* uplink = childNamed(*sub, QStringLiteral("uplink"));
		REQUIRE(uplink != nullptr);
		CHECK(uplink->entry.kind == OperationEntryKind::DirectoryLink);
		CHECK(uplink->children.empty());
	}

	SECTION("mutual links terminate after one lap per side")
	{
		REQUIRE(QDir{}.mkpath(base % "/root/dirA"));
		REQUIRE(QDir{}.mkpath(base % "/root/dirB"));
		REQUIRE(createDirectoryLink(base % "/root/dirB", base % "/root/dirA/linkToB"));
		REQUIRE(createDirectoryLink(base % "/root/dirA", base % "/root/dirB/linkToA"));

		const SourceNode tree = buildTree(base % "/root", SourceTreeBuildMode::MaterializingTransfer);
		// root, dirA, dirB, and each link traversed once with the counter-link as a terminated leaf:
		// dirA/linkToB -> (borrowed linkToA leaf), dirB/linkToA -> (borrowed linkToB leaf)
		CHECK(tree.subtreeItems == 7);

		const SourceNode* dirA = childNamed(tree, QStringLiteral("dirA"));
		REQUIRE(dirA != nullptr);
		const SourceNode* linkToB = childNamed(*dirA, QStringLiteral("linkToB"));
		REQUIRE(linkToB != nullptr);
		REQUIRE(linkToB->children.size() == 1);
		CHECK(linkToB->children.front().children.empty()); // The counter-link did not recurse
		CHECK(linkToB->children.front().ownership == SourceOwnership::BorrowedThroughDirectoryLink);
	}
}

TEST_CASE("source tree: scanning progress and cancellation", "[sourcetree]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root/sub1"));
	REQUIRE(QDir{}.mkpath(base % "/root/sub2"));
	writeTestFile(base % "/root/sub1/a.bin", QByteArray(100, 'a'));
	writeTestFile(base % "/root/sub2/b.bin", QByteArray(200, 'b'));

	SECTION("progress is Scanning-phased, counting, and total-free")
	{
		ScanScript script;
		auto context = scanContext(script);
		const auto result = buildSourceTree(context, snapshotOf(base % "/root"), SourceTreeBuildMode::MaterializingTransfer);
		REQUIRE(std::holds_alternative<SourceNode>(result));

		REQUIRE(script.progress.size() == 5); // One per discovered entry
		for (size_t i = 0; i < script.progress.size(); ++i)
		{
			const ProgressSnapshot& snapshot = script.progress[i];
			CHECK(snapshot.phase == OperationPhase::Scanning);
			CHECK(snapshot.itemsProcessed == i + 1);
			CHECK(!snapshot.itemsTotal.has_value());
			CHECK(!snapshot.bytesTotal.has_value());
			CHECK(snapshot.currentEntry.has_value());
		}
	}

	SECTION("cancellation at a checkpoint abandons the build")
	{
		ScanScript script{ .checkpointsBeforeCancel = 1 };
		auto context = scanContext(script);
		const auto result = buildSourceTree(context, snapshotOf(base % "/root"), SourceTreeBuildMode::MaterializingTransfer);
		CHECK(std::holds_alternative<ScanCancelled>(result));
	}
}

#ifndef _WIN32
TEST_CASE("source tree: an unreadable directory fails the build with the failing entry", "[sourcetree]")
{
	if (::geteuid() == 0)
	{
		WARN("Running as root: permissions cannot be enforced, skipping");
		return;
	}

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root/locked"));
	writeTestFile(base % "/root/locked/hidden.bin", QByteArray(10, 'x'));
	REQUIRE(::chmod(QFile::encodeName(base % "/root/locked").constData(), 0) == 0);

	ScanScript script;
	auto context = scanContext(script);
	const auto result = buildSourceTree(context, snapshotOf(base % "/root"), SourceTreeBuildMode::MaterializingTransfer);

	REQUIRE(::chmod(QFile::encodeName(base % "/root/locked").constData(), 0755) == 0); // Restore for cleanup

	REQUIRE(std::holds_alternative<OperationDiagnostic>(result));
	const OperationDiagnostic& diagnostic = std::get<OperationDiagnostic>(result);
	CHECK(diagnostic.failure.action == FailedAction::InspectSource);
	CHECK(diagnostic.failure.filesystemError.category == FileErrorCategory::PermissionDenied);
	CHECK(diagnostic.source.path.value() == ep(base % "/root/locked").value());
}
#endif

TEST_CASE("source tree: file and link roots are leaf manifests", "[sourcetree]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/file.bin", QByteArray(1234, 'f'));

	const SourceNode tree = buildTree(base % "/file.bin", SourceTreeBuildMode::MaterializingTransfer);
	CHECK(tree.entry.kind == OperationEntryKind::RegularFile);
	CHECK(tree.subtreeItems == 1);
	CHECK(tree.subtreeBytes == 1234);
	CHECK(tree.children.empty());
}
