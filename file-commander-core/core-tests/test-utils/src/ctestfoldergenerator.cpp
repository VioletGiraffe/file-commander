#include "ctestfoldergenerator.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QStringBuilder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr size_t minNameLength = 1;
constexpr size_t maxNameLength = 40;

// A tree nobody asked to be long must not become one by accident: Windows' MAX_PATH is 260, and a case that trips
// over it while testing something else fails for a reason it never meant to cover. TreeRecipe::minPathLength is
// how a test asks to go past it deliberately.
constexpr qsizetype maxOrdinaryPathLength = 240;

// Inside the 255-byte limit on one component that all three target filesystems share.
constexpr qsizetype longComponentLength = 200;

// One entry per character's worth of text rather than a string of characters: a non-BMP character is two UTF-16
// code units, and drawing code units separately would produce lone surrogates.
//
// Every atom has to survive a round trip through all three target filesystems unchanged, because a generated tree
// is compared whole - there is no per-name "skip if the filesystem rewrote it" escape hatch here of the kind the
// hostile-name table has. So nothing Windows forbids in a name, and nothing macOS would renormalize, which is why
// the non-ASCII end is CJK and a non-BMP character rather than accented Latin.
std::vector<QString> nameAlphabet()
{
	std::vector<QString> atoms;

	for (char16_t c = u'a'; c <= u'z'; ++c)
		atoms.emplace_back(QChar{ c });
	for (char16_t c = u'A'; c <= u'Z'; ++c)
		atoms.emplace_back(QChar{ c });
	for (char16_t c = u'0'; c <= u'9'; ++c)
		atoms.emplace_back(QChar{ c });
	for (const QChar c : QStringLiteral(" -_.,;!#$%&()[]{}@^+=~'"))
		atoms.emplace_back(c);

	atoms.emplace_back(QChar{ 0x6587 }); // CJK, inside the BMP
	atoms.emplace_back(QChar{ 0x4EF6 });

	const char32_t rocket = 0x1F680;
	atoms.emplace_back(QString::fromUcs4(&rocket, 1));

	return atoms;
}

} // namespace

void CTestFolderGenerator::setSeed(const uint32_t seed)
{
	_randomGenerator.setSeed(seed);
}

std::optional<GeneratedTree> CTestFolderGenerator::generateRandomTree(const QString& parentDir, const TreeRecipe& recipe)
{
	if (!QDir{ parentDir }.exists())
		return {};

	GeneratedTree tree;
	QString root = parentDir;
	if (recipe.minPathLength > 0)
	{
		const auto nestedRoot = nestRootToPathLength(parentDir, recipe.minPathLength, tree);
		if (!nestedRoot)
			return {};

		root = *nestedRoot;
	}

	struct Directory
	{
		QString path;
		std::vector<QString> usedFoldedNames;
	};

	// Every directory made so far is a candidate parent for the next entry, so depth grows on its own rather than
	// being budgeted; a uniform choice leaves most of the tree shallow and some of it deep. Index 0 is the root.
	std::vector<Directory> directories;
	directories.push_back({ .path = root });

	const bool capPathLength = recipe.minPathLength == 0;
	const auto pickParentIndex = [&] {
		const size_t index = _randomGenerator.randomNumber<size_t>(0, directories.size() - 1);
		const bool fitsLongestName = directories[index].path.length() + 1 + static_cast<qsizetype>(maxNameLength) <= maxOrdinaryPathLength;
		if (capPathLength && !fitsLongestName)
			return size_t{ 0 }; // Back to the root rather than one level deeper

		return index;
	};

	for (size_t i = 0; i < recipe.numFolders; ++i)
	{
		const size_t parent = pickParentIndex();
		const QString name = unusedChildName(directories[parent].usedFoldedNames, minNameLength, maxNameLength);
		if (name.isEmpty())
			return {};

		const QString path = directories[parent].path % '/' % name;
		if (!QDir{}.mkpath(path))
			return {};

		directories.push_back({ .path = path });
		tree.folders.push_back(path);
	}

	for (const uint64_t size : plannedFileSizes(recipe))
	{
		const size_t parent = pickParentIndex();
		const QString name = unusedChildName(directories[parent].usedFoldedNames, minNameLength, maxNameLength);
		if (name.isEmpty())
			return {};

		const QString path = directories[parent].path % '/' % name;
		if (!createFileOfSize(path, size))
			return {};

		tree.files.push_back(path);
		tree.totalBytes += size;
	}

	// One component near the length limit, in the root: a copy rebases it under a destination path that can be
	// longer than the source's, and it is that sum which overflows.
	const qsizetype longNameLength = std::min(longComponentLength, maxOrdinaryPathLength - root.length() - 1);
	if (longNameLength >= static_cast<qsizetype>(maxNameLength))
	{
		constexpr uint64_t longNameFileSize = 64; // The name is the subject here, not the contents
		const QString path = root % '/' % _randomGenerator.randomString(static_cast<size_t>(longNameLength));
		if (!createFileOfSize(path, longNameFileSize))
			return {};

		tree.files.push_back(path);
		tree.totalBytes += longNameFileSize;
	}

	return tree;
}

QString CTestFolderGenerator::randomName(const size_t minLength, const size_t maxLength)
{
	static const std::vector<QString> alphabet = nameAlphabet();

	QString name;
	const size_t length = _randomGenerator.randomNumber(minLength, maxLength);
	for (size_t i = 0; i < length; ++i)
		name += alphabet[_randomGenerator.randomNumber<size_t>(0, alphabet.size() - 1)];

	// Win32 normalization strips a trailing space or period from a name, and a leading space is at best uncertain;
	// either way the entry would end up reachable only through the \\?\ prefix. Names like that belong to the
	// hostile-name table, which can skip a case per filesystem - something a whole-tree comparison cannot do.
	while (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
		name.chop(1);
	while (name.startsWith(QLatin1Char(' ')))
		name.remove(0, 1);

	// Trimming leaves nothing at all behind for a name that was only dots and spaces - "." and ".." among them.
	if (name.isEmpty())
		return QStringLiteral("x");

	// Windows resolves these to devices rather than to files whatever the extension or the letter case, and a QFile
	// opened on one writes to the device. About one random name in a million is one: rare, but a stress run makes
	// enough names for it to come up eventually, and it would present as an entry that simply went missing.
	static const QStringList reservedStems{
		QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
		QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
		QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
		QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
		QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
		QStringLiteral("LPT8"), QStringLiteral("LPT9")
	};

	if (reservedStems.contains(name.left(name.indexOf(QLatin1Char('.'))), Qt::CaseInsensitive))
		name.prepend(QLatin1Char('x'));

	return name;
}

QString CTestFolderGenerator::unusedChildName(std::vector<QString>& usedFoldedNames, const size_t minLength, const size_t maxLength)
{
	// Windows and macOS refuse a second name in one directory differing only in letter case, and macOS folds
	// Unicode normalization too, so what has to be unique is the folded form rather than the name.
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		QString name = randomName(minLength, maxLength);
		QString folded = name.normalized(QString::NormalizationForm_C).toCaseFolded();
		if (std::find(usedFoldedNames.begin(), usedFoldedNames.end(), folded) != usedFoldedNames.end())
			continue;

		usedFoldedNames.push_back(std::move(folded));
		return name;
	}

	return {};
}

std::vector<uint64_t> CTestFolderGenerator::plannedFileSizes(const TreeRecipe& recipe)
{
	std::vector<uint64_t> sizes;
	sizes.reserve(recipe.numFiles);

	sizes.push_back(0); // A category of its own, not something to hope the random sizes land on
	if (recipe.chunkSize > 0)
	{
		// Where a chunked copy loop breaks: the block boundary itself, and one byte either side of it, which is
		// where an off-by-one in the loop condition shows up.
		const uint64_t chunk = recipe.chunkSize;
		sizes.insert(sizes.end(), { chunk - 1, chunk, chunk + 1, 2 * chunk - 1, 2 * chunk, 2 * chunk + 1 });
	}

	if (sizes.size() > recipe.numFiles)
		sizes.resize(recipe.numFiles);

	while (sizes.size() < recipe.numFiles)
		sizes.push_back(_randomGenerator.randomNumber<uint64_t>(0, recipe.maxFileSize));

	// Fisher-Yates by hand, because std::shuffle's mapping onto the engine is unspecified and this class exists to
	// be reproducible. Unshuffled, every boundary size would land in whichever directory came first.
	for (size_t i = sizes.size(); i > 1; --i)
		std::swap(sizes[i - 1], sizes[_randomGenerator.randomNumber<size_t>(0, i - 1)]);

	return sizes;
}

bool CTestFolderGenerator::createFileOfSize(const QString& path, uint64_t size)
{
	QFile file{ path };
	if (!file.open(QFile::WriteOnly))
		return false;

	// Random bytes rather than a repeating pattern: against a pattern whose period divides the chunk size, a copy
	// that duplicated or dropped an entire chunk would still compare equal.
	QByteArray contents;
	contents.reserve(static_cast<qsizetype>(size));

	for (; size >= sizeof(uint64_t); size -= sizeof(uint64_t))
	{
		const uint64_t randomBytes = _randomGenerator.randomNumber<uint64_t>(0, std::numeric_limits<uint64_t>::max());
		contents.append(reinterpret_cast<const char*>(&randomBytes), sizeof(randomBytes));
	}

	while (size-- > 0)
		contents.append(static_cast<char>(_randomGenerator.randomNumber<uint8_t>(0, 255)));

	return file.write(contents) == contents.size();
}

std::optional<QString> CTestFolderGenerator::nestRootToPathLength(const QString& parentDir, const size_t minPathLength, GeneratedTree& tree)
{
	QString path = parentDir;
	while (static_cast<size_t>(path.length()) < minPathLength)
	{
		// 'A'-'Z' only: one byte per character makes the 255-byte component limit and the 255-character one the
		// same thing on every target filesystem.
		path += QLatin1Char('/');
		path += _randomGenerator.randomString(static_cast<size_t>(longComponentLength));
		if (!QDir{}.mkpath(path))
			return {};

		tree.folders.push_back(path);
	}

	return path;
}
