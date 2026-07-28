// The single policy check for a name a user proposes: which rule each input breaks, and that nothing is
// ever repaired to make it pass.

#include "fileoperations/newnamecheck.h"

#include "3rdparty/catch2/catch.hpp"

TEST_CASE("checkNewEntryName: text that is not one name", "[newnamecheck]")
{
	CHECK(checkNewEntryName({}) == NameRejection::Empty);
	QString nameWithNull = QStringLiteral("victim");
	nameWithNull += QChar{ 0 };
	nameWithNull += QStringLiteral("suffix");
	CHECK(checkNewEntryName(nameWithNull) == NameRejection::InvalidCharacter);
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
#ifdef _WIN32
	CHECK(checkNewEntryName(QStringLiteral("\t")) == NameRejection::InvalidCharacter);
#else
	CHECK(checkNewEntryName(QStringLiteral("\t")) == NameRejection::WhitespaceOnly);
#endif

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

TEST_CASE("checkNewEntryName: Windows refuses native-forbidden characters", "[newnamecheck]")
{
	for (const QString& name : { QStringLiteral("a:b"), QStringLiteral("a<b"), QStringLiteral("a>b"), QStringLiteral("a\"b"),
		QStringLiteral("a|b"), QStringLiteral("a?b"), QStringLiteral("a*b") })
	{
		INFO("name: " << name.toStdString());
		CHECK(checkNewEntryName(name) == NameRejection::InvalidCharacter);
	}

	QString nameWithControl = QStringLiteral("a");
	nameWithControl += QChar{ 0x1f };
	nameWithControl += QLatin1Char('b');
	CHECK(checkNewEntryName(nameWithControl) == NameRejection::InvalidCharacter);
}

TEST_CASE("checkNewEntryName: Windows refuses reserved device names with any extension", "[newnamecheck]")
{
	for (const QString& name : { QStringLiteral("CON"), QStringLiteral("prn"), QStringLiteral("Aux"), QStringLiteral("NUL"),
		QStringLiteral("COM1"), QStringLiteral("com9.log"), QStringLiteral("LPT1"), QStringLiteral("lpt9.txt"), QStringLiteral("NUL.txt") })
	{
		INFO("name: " << name.toStdString());
		CHECK(checkNewEntryName(name) == NameRejection::ReservedDeviceName);
	}

	for (const QString& name : { QStringLiteral("CONSOLE"), QStringLiteral("NUL_name"), QStringLiteral("COM0"),
		QStringLiteral("COM10"), QStringLiteral("LPT0"), QStringLiteral("LPT10") })
	{
		INFO("name: " << name.toStdString());
		CHECK(checkNewEntryName(name) == NameRejection::None);
	}
}

#else

TEST_CASE("checkNewEntryName: a trailing dot or space is an ordinary POSIX name", "[newnamecheck]")
{
	CHECK(checkNewEntryName(QStringLiteral("name ")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("name.")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("...")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("NUL.txt")) == NameRejection::None);
	CHECK(checkNewEntryName(QStringLiteral("a:b?*")) == NameRejection::None);
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

	QString pathWithNull = QStringLiteral("a/victim");
	pathWithNull += QChar{ 0 };
	pathWithNull += QStringLiteral("suffix/c");
	CHECK(checkNewEntryPath(pathWithNull) == NameRejection::InvalidCharacter);

#ifdef _WIN32
	CHECK(checkNewEntryPath(QStringLiteral("a/NUL/b")) == NameRejection::ReservedDeviceName);
	CHECK(checkNewEntryPath(QStringLiteral("a/good:b")) == NameRejection::InvalidCharacter);
#endif
}
