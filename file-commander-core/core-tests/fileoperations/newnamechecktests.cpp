// The single policy check for a name a user proposes: which rule each input breaks, and that nothing is
// ever repaired to make it pass.

#include "fileoperations/newnamecheck.h"

#include "3rdparty/catch2/catch.hpp"

TEST_CASE("checkNewEntryName: text that is not one name", "[newnamecheck]")
{
	CHECK(checkNewEntryName({}) == NameRejection::Empty);
	CHECK(checkNewEntryName(QStringLiteral("a/b")) == NameRejection::Separator);
	CHECK(checkNewEntryName(QStringLiteral(".")) == NameRejection::DotComponent);
	CHECK(checkNewEntryName(QStringLiteral("..")) == NameRejection::DotComponent);

#ifdef _WIN32
	CHECK(checkNewEntryName(QStringLiteral("a\\b")) == NameRejection::Separator);
#else
	CHECK(checkNewEntryName(QStringLiteral("a\\b")) == NameRejection::None); // An ordinary character in a POSIX name
#endif
}

TEST_CASE("checkNewEntryName: whitespace alone is refused rather than trimmed away", "[newnamecheck]")
{
	CHECK(checkNewEntryName(QStringLiteral(" ")) == NameRejection::WhitespaceOnly);
	CHECK(checkNewEntryName(QStringLiteral("   ")) == NameRejection::WhitespaceOnly);
	CHECK(checkNewEntryName(QStringLiteral("\t")) == NameRejection::WhitespaceOnly);

	// Judged as entered: a trim would have turned these into "a" and the rejected ".".
	CHECK(checkNewEntryName(QStringLiteral(" a")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral(". ")) != NameRejection::DotComponent);
}

#ifdef _WIN32

// Storable on NTFS through the extended-length prefix this app uses, but Win32 normalization strips them, so
// Explorer and everything else would look for a different name and never find the entry.
TEST_CASE("checkNewEntryName: Windows refuses a trailing dot or space", "[newnamecheck]")
{
	CHECK(checkNewEntryName(QStringLiteral("name ")) == NameRejection::TrailingDotOrSpace);
	CHECK(checkNewEntryName(QStringLiteral("name.")) == NameRejection::TrailingDotOrSpace);
	CHECK(checkNewEntryName(QStringLiteral("...")) == NameRejection::TrailingDotOrSpace);
	CHECK(checkNewEntryName(QStringLiteral("name.txt")) == NameRejection::None);
}

#else

TEST_CASE("checkNewEntryName: a trailing dot or space is an ordinary POSIX name", "[newnamecheck]")
{
	CHECK(checkNewEntryName(QStringLiteral("name ")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("name.")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("...")) == NameRejection::None);
}

#endif

TEST_CASE("checkNewEntryPath: every component is judged", "[newnamecheck]")
{
	CHECK(checkNewEntryPath({}) == NameRejection::Empty);
	CHECK(checkNewEntryPath(QStringLiteral("/")) == NameRejection::Empty); // Separators alone name nothing

	CHECK(checkNewEntryPath(QStringLiteral("a")) == NameRejection::None);
	CHECK(checkNewEntryPath(QStringLiteral("a/b/c")) == NameRejection::None);
	CHECK(checkNewEntryPath(QStringLiteral("a//b")) == NameRejection::None); // Empty parts skipped, as when parsing

	CHECK(checkNewEntryPath(QStringLiteral("a/ /c")) == NameRejection::WhitespaceOnly);
	CHECK(checkNewEntryPath(QStringLiteral("a/../c")) == NameRejection::DotComponent);
}
