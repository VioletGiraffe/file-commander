#pragma once
#include "crandomdatagenerator.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

// What to generate. The defaults make a small, quick tree; the numbers that make a case interesting are the
// caller's to choose.
struct TreeRecipe
{
	// Files of random size and name. Where the path leaves room, one more is added with a near-limit-length name.
	size_t numFiles = 50;
	size_t numFolders = 10;
	uint64_t maxFileSize = 4096;
	// Non-zero: also emit files of exactly this size, of twice it, and of one byte either side of both - the
	// boundaries a chunked copy loop gets wrong. Pass the chunk size the code under test will use.
	uint64_t chunkSize = 0;
	// Non-zero: nest the tree under a chain of long-named directories until absolute paths reach this many
	// characters, for the cases past Windows' MAX_PATH. Ordinary trees stay well clear of that limit.
	size_t minPathLength = 0;
};

// What was created, so that a test can assert against it without walking the tree again.
struct GeneratedTree
{
	std::vector<QString> files; // Absolute paths
	std::vector<QString> folders;
	uint64_t totalBytes = 0;
};

// Random directory trees, for tests that need bulk rather than a hand-built shape whose entries they name.
// Reproducible: the same seed yields the same tree, so a failure reported with its seed can be replayed.
class CTestFolderGenerator
{
public:
	void setSeed(uint32_t seed);

	// Empty if anything could not be created, which the caller is expected to fail the test on.
	[[nodiscard]] std::optional<GeneratedTree> generateRandomTree(const QString& parentDir, const TreeRecipe& recipe);

private:
	// A name every target filesystem stores verbatim; the alphabet in the .cpp says what that excludes.
	[[nodiscard]] QString randomName(size_t minLength, size_t maxLength);
	// Empty if no unused name turned up, which for the lengths in use means far too many were asked for.
	[[nodiscard]] QString unusedChildName(std::vector<QString>& usedFoldedNames, size_t minLength, size_t maxLength);
	[[nodiscard]] std::vector<uint64_t> plannedFileSizes(const TreeRecipe& recipe);
	[[nodiscard]] bool createFileOfSize(const QString& path, uint64_t size);
	// The new root, deeper than the one passed in; every level it creates is recorded in the tree.
	[[nodiscard]] std::optional<QString> nestRootToPathLength(const QString& parentDir, size_t minPathLength, GeneratedTree& tree);

	CRandomDataGenerator _randomGenerator;
};
