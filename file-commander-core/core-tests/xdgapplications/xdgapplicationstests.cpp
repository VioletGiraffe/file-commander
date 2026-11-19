#include "shell/xdgapplications.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <map>
#include <utility>
#include <vector>

using namespace XdgApplications;

namespace {

// The XDG folders, all inside one temporary folder
struct TestFolders
{
	QTemporaryDir root;
	Environment environment{
		.configHome = root.path() % QStringLiteral("/config"),
		.configDirs = { root.path() % QStringLiteral("/etc-xdg") },
		.dataHome = root.path() % QStringLiteral("/data"),
		.dataDirs = { root.path() % QStringLiteral("/share1"), root.path() % QStringLiteral("/share2") },
		.desktops = {},
		.localeName = QStringLiteral("de_DE"),
	};

	TestFolders()
	{
		REQUIRE(root.isValid());
	}

	// `relativePath` is relative to the temporary folder
	void write(const QString& relativePath, const QString& text) const
	{
		const QString path = root.path() % '/' % relativePath;
		REQUIRE(QDir{}.mkpath(QFileInfo{ path }.absolutePath()));
		QFile file{ path };
		REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		REQUIRE(file.write(text.toUtf8()) == text.toUtf8().size());
	}

	// An application entry in an applications folder, e.g. "share1/applications/viewer.desktop".
	// `keys` add to or replace Type, Name (the file's base name), Exec and MimeType; their values are written as given.
	void writeEntry(const QString& relativePath, const QString& mimeTypes, const std::map<QString, QString>& keys = {}) const
	{
		std::map<QString, QString> entryKeys{
			{ QStringLiteral("Type"), QStringLiteral("Application") },
			{ QStringLiteral("Name"), QFileInfo{ relativePath }.completeBaseName() },
			{ QStringLiteral("Exec"), QStringLiteral("program %f") },
			{ QStringLiteral("MimeType"), mimeTypes },
		};
		for (const auto& [key, value] : keys)
			entryKeys[key] = value;

		QString text = QStringLiteral("[Desktop Entry]\n");
		for (const auto& [key, value] : entryKeys)
			text += key % '=' % value % '\n';
		write(relativePath, text);
	}

	[[nodiscard]] QStringList ids(const std::vector<QStringList>& fileTypes) const
	{
		QStringList result;
		for (const Application& application : applicationsForFileTypes(fileTypes, environment))
			result.push_back(application.id);
		return result;
	}

	[[nodiscard]] Application onlyApplication(const QString& mimeType) const
	{
		const std::vector<Application> applications = applicationsForFileTypes({ QStringList{ mimeType } }, environment);
		REQUIRE(applications.size() == 1);
		return applications.front();
	}
};

}

static const QStringList textFile{ QStringLiteral("text/plain") };
static const QStringList imageFile{ QStringLiteral("image/png") };

TEST_CASE("An application is listed for the MIME types its entry declares", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/viewer.desktop"), QStringLiteral("text/plain;image/png;"));
	folders.writeEntry(QStringLiteral("share2/applications/painter.desktop"), QStringLiteral("image/png"));

	CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("viewer.desktop") });
	// The more important folder's entries first
	CHECK(folders.ids({ imageFile }) == QStringList{ QStringLiteral("viewer.desktop"), QStringLiteral("painter.desktop") });
	CHECK(folders.ids({ QStringList{ QStringLiteral("audio/flac") } }).isEmpty());
	CHECK(folders.ids({}).isEmpty());
}

TEST_CASE("A desktop file ID includes its subfolders, and the most important folder's entry is used", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("data/applications/kde/kate.desktop"), QStringLiteral("text/plain"), { { QStringLiteral("Name"), QStringLiteral("Home") } });
	folders.writeEntry(QStringLiteral("share1/applications/kde-kate.desktop"), QStringLiteral("text/plain"), { { QStringLiteral("Name"), QStringLiteral("System") } });

	const Application application = folders.onlyApplication(QStringLiteral("text/plain"));
	CHECK(application.id == QStringLiteral("kde-kate.desktop"));
	CHECK(application.name == QStringLiteral("Home"));
	CHECK(application.desktopFilePath == folders.root.path() % QStringLiteral("/data/applications/kde/kate.desktop"));
}

TEST_CASE("A hidden entry removes the entry it overrides", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("data/applications/viewer.desktop"), QStringLiteral("text/plain"), { { QStringLiteral("Hidden"), QStringLiteral("true") } });
	folders.writeEntry(QStringLiteral("share1/applications/viewer.desktop"), QStringLiteral("text/plain"));

	CHECK(folders.ids({ textFile }).isEmpty());
}

TEST_CASE("An entry that is no application, has no command line, or names a missing program is left out", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/link.desktop"), QStringLiteral("text/plain"), { { QStringLiteral("Type"), QStringLiteral("Link") } });
	folders.writeEntry(QStringLiteral("share1/applications/noexec.desktop"), QStringLiteral("text/plain"), { { QStringLiteral("Exec"), QString{} } });
	folders.writeEntry(QStringLiteral("share1/applications/missing.desktop"), QStringLiteral("text/plain"),
		{ { QStringLiteral("TryExec"), folders.root.path() % QStringLiteral("/no-such-program") } });
	folders.write(QStringLiteral("share1/applications/nogroup.desktop"), QStringLiteral("Type=Application\nExec=program\nMimeType=text/plain\n"));

	CHECK(folders.ids({ textFile }).isEmpty());
}

TEST_CASE("The default application leads, the first installed one its list names", "[xdgapplications]")
{
	TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/a.desktop"), QStringLiteral("text/plain"));
	folders.writeEntry(QStringLiteral("share1/applications/b.desktop"), QStringLiteral("text/plain"));

	SECTION("from the most important list")
	{
		folders.write(QStringLiteral("share1/applications/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=a.desktop\n"));
		folders.write(QStringLiteral("config/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=missing.desktop;b.desktop;\n"));
		CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("b.desktop"), QStringLiteral("a.desktop") });
	}

	SECTION("from the desktop's own list before the generic one")
	{
		folders.environment.desktops = { QStringLiteral("kde") };
		folders.write(QStringLiteral("config/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=a.desktop\n"));
		folders.write(QStringLiteral("config/kde-mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=b.desktop\n"));
		CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("b.desktop"), QStringLiteral("a.desktop") });
	}

	SECTION("from a folder's former defaults.list")
	{
		folders.write(QStringLiteral("share1/applications/defaults.list"), QStringLiteral("[Default Applications]\ntext/plain=b.desktop\n"));
		CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("b.desktop"), QStringLiteral("a.desktop") });
	}

	SECTION("even when the entry does not declare the type")
	{
		folders.writeEntry(QStringLiteral("share2/applications/c.desktop"), QStringLiteral("image/png"));
		folders.write(QStringLiteral("config/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=c.desktop\n"));
		CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("c.desktop"), QStringLiteral("a.desktop"), QStringLiteral("b.desktop") });
	}
}

TEST_CASE("Added associations list an application, removed ones hide it from less important folders", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/added.desktop"), QStringLiteral("image/png"));
	folders.writeEntry(QStringLiteral("share1/applications/removed.desktop"), QStringLiteral("text/plain"));
	folders.writeEntry(QStringLiteral("share1/applications/kept.desktop"), QStringLiteral("text/plain"));
	folders.write(QStringLiteral("config/mimeapps.list"),
		QStringLiteral("[Added Associations]\ntext/plain=added.desktop;kept.desktop;\n\n[Removed Associations]\ntext/plain=removed.desktop;\n"));
	// Less important than the config folder's list, which already added kept.desktop
	folders.write(QStringLiteral("data/applications/mimeapps.list"), QStringLiteral("[Removed Associations]\ntext/plain=kept.desktop\n"));

	CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("added.desktop"), QStringLiteral("kept.desktop") });
}

TEST_CASE("A file type's own applications come before its ancestors', and an ancestor's default leads", "[xdgapplications]")
{
	TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/cpp.desktop"), QStringLiteral("text/x-c++src"));
	folders.writeEntry(QStringLiteral("share1/applications/text.desktop"), QStringLiteral("text/plain"));
	const QStringList sourceFile{ QStringLiteral("text/x-c++src"), QStringLiteral("text/plain") };

	CHECK(folders.ids({ sourceFile }) == QStringList{ QStringLiteral("cpp.desktop"), QStringLiteral("text.desktop") });

	folders.write(QStringLiteral("config/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=text.desktop\n"));
	CHECK(folders.ids({ sourceFile }) == QStringList{ QStringLiteral("text.desktop"), QStringLiteral("cpp.desktop") });
}

TEST_CASE("Files of several types get the applications that open them all", "[xdgapplications]")
{
	const TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/both.desktop"), QStringLiteral("text/plain;image/png"));
	folders.writeEntry(QStringLiteral("share1/applications/text.desktop"), QStringLiteral("text/plain"));
	folders.write(QStringLiteral("config/mimeapps.list"), QStringLiteral("[Default Applications]\ntext/plain=text.desktop\n"));

	CHECK(folders.ids({ textFile, imageFile }) == QStringList{ QStringLiteral("both.desktop") });
	CHECK(folders.ids({ textFile, textFile }) == QStringList{ QStringLiteral("text.desktop"), QStringLiteral("both.desktop") });
}

TEST_CASE("The name is in the environment's language where the entry has it", "[xdgapplications]")
{
	TestFolders folders;
	folders.writeEntry(QStringLiteral("share1/applications/editor.desktop"), QStringLiteral("text/plain"),
		{ { QStringLiteral("Name[de]"), QStringLiteral("Bearbeiter") }, { QStringLiteral("Name[de_DE]"), QStringLiteral("Editor DE") } });

	CHECK(folders.onlyApplication(QStringLiteral("text/plain")).name == QStringLiteral("Editor DE"));
	folders.environment.localeName = QStringLiteral("de_AT");
	CHECK(folders.onlyApplication(QStringLiteral("text/plain")).name == QStringLiteral("Bearbeiter"));
	folders.environment.localeName = QStringLiteral("fr_FR");
	CHECK(folders.onlyApplication(QStringLiteral("text/plain")).name == QStringLiteral("editor"));
}

TEST_CASE("Values are unescaped, and a list's escaped semicolon belongs to its entry", "[xdgapplications]")
{
	const TestFolders folders;
	folders.write(QStringLiteral("share1/applications/escapes.desktop"),
		QStringLiteral("[Desktop Entry]\n# A comment\nType = Application\nName=\\sTwo\\tparts\\\\ \\q\nExec=program\nMimeType=text/a\\;b;text/plain;;\nTerminal=true\nPath=/work dir\n"));

	const Application application = folders.onlyApplication(QStringLiteral("text/a;b"));
	CHECK(application.name == QStringLiteral(" Two\tparts\\ \\q"));
	CHECK(application.terminal);
	CHECK(application.workingDir == QStringLiteral("/work dir"));
	CHECK(folders.ids({ textFile }) == QStringList{ QStringLiteral("escapes.desktop") });
}

static Application applicationWithExec(const QString& exec)
{
	return { .id = QStringLiteral("app.desktop"), .desktopFilePath = QStringLiteral("/apps/app.desktop"), .name = QStringLiteral("App"),
		.icon = QStringLiteral("app-icon"), .exec = exec, .workingDir = {}, .terminal = false };
}

static const QStringList twoFiles{ QStringLiteral("/a b/one"), QStringLiteral("/two %f") };

TEST_CASE("%F and %U pass every file to one command line", "[xdgapplications][exec]")
{
	CHECK(commandLines(applicationWithExec(QStringLiteral("program --all %F")), twoFiles)
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("--all"), QStringLiteral("/a b/one"), QStringLiteral("/two %f") } });
	CHECK(commandLines(applicationWithExec(QStringLiteral("program %U")), twoFiles)
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("/a b/one"), QStringLiteral("/two %f") } });
}

TEST_CASE("%f and %u start a command line per file, inside a word too", "[xdgapplications][exec]")
{
	CHECK(commandLines(applicationWithExec(QStringLiteral("program --file=%u")), twoFiles)
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("--file=/a b/one") }, { QStringLiteral("program"), QStringLiteral("--file=/two %f") } });
	CHECK(commandLines(applicationWithExec(QStringLiteral("program %f")), {}) == std::vector<QStringList>{ { QStringLiteral("program") } });
}

TEST_CASE("Without a file field code the files are not passed", "[xdgapplications][exec]")
{
	CHECK(commandLines(applicationWithExec(QStringLiteral("program --new-window")), twoFiles)
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("--new-window") } });
}

TEST_CASE("The other field codes expand to the entry's values, and the deprecated ones to nothing", "[xdgapplications][exec]")
{
	CHECK(commandLines(applicationWithExec(QStringLiteral("program %i --name=%c %k 100%% %d %m x%n")), {})
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("--icon"), QStringLiteral("app-icon"), QStringLiteral("--name=App"),
			QStringLiteral("/apps/app.desktop"), QStringLiteral("100%"), QStringLiteral("x") } });

	Application withoutIcon = applicationWithExec(QStringLiteral("program %i \"\""));
	withoutIcon.icon.clear();
	CHECK(commandLines(withoutIcon, {}) == std::vector<QStringList>{ { QStringLiteral("program"), QString{} } });
}

TEST_CASE("A command line that cannot be split, or has no program, is an error", "[xdgapplications][exec]")
{
	CHECK(!commandLines(applicationWithExec(QStringLiteral("program \"unfinished")), twoFiles));
	CHECK(!commandLines(applicationWithExec(QStringLiteral("%f")), {}));
}

TEST_CASE("Exec is unescaped as a string value before its quoting applies", "[xdgapplications][exec]")
{
	const TestFolders folders;
	// The file holds "\\$HOME": the string value is "\$HOME", the quoted argument $HOME
	folders.writeEntry(QStringLiteral("share1/applications/quoted.desktop"), QStringLiteral("text/plain"),
		{ { QStringLiteral("Exec"), QStringLiteral("program \"a b\" \"\\\\$HOME\" %f") } });

	CHECK(commandLines(folders.onlyApplication(QStringLiteral("text/plain")), { QStringLiteral("/file") })
		== std::vector<QStringList>{ { QStringLiteral("program"), QStringLiteral("a b"), QStringLiteral("$HOME"), QStringLiteral("/file") } });
}

TEST_CASE("What escapedExecArgument and escapedStringValue write reads back unchanged", "[xdgapplications][exec]")
{
	const TestFolders folders;
	const QString program = QStringLiteral("/opt/My \"App\"/`$bin`\\%f 100%");
	const QString icon = QStringLiteral("/icons/back\\slash\nnew line");
	folders.write(QStringLiteral("share1/applications/written.desktop"), QStringLiteral("[Desktop Entry]\nType=Application\nName=Written\nMimeType=text/plain\nExec=")
		% escapedExecArgument(program) % QStringLiteral(" %F\nIcon=") % escapedStringValue(icon) % '\n');

	const Application application = folders.onlyApplication(QStringLiteral("text/plain"));
	CHECK(application.icon == icon);
	CHECK(commandLines(application, { QStringLiteral("/file") }) == std::vector<QStringList>{ { program, QStringLiteral("/file") } });
}

#ifndef _WIN32
TEST_CASE("currentEnvironment ignores relative folders and falls back to the spec's defaults", "[xdgapplications]")
{
	const char* const variables[]{ "XDG_CONFIG_HOME", "XDG_CONFIG_DIRS", "XDG_DATA_HOME", "XDG_DATA_DIRS", "XDG_CURRENT_DESKTOP" };
	std::vector<std::pair<const char*, QByteArray>> savedValues;
	for (const char* variable : variables)
		savedValues.emplace_back(variable, qgetenv(variable));
	EXEC_ON_SCOPE_EXIT([&savedValues] {
		for (const auto& [variable, value] : savedValues)
		{
			if (value.isNull())
				qunsetenv(variable);
			else
				qputenv(variable, value);
		}
	});

	qputenv("XDG_CONFIG_HOME", "relative/config");
	qunsetenv("XDG_CONFIG_DIRS");
	qputenv("XDG_DATA_HOME", "/home data");
	qputenv("XDG_DATA_DIRS", "relative:/first::/second");
	qputenv("XDG_CURRENT_DESKTOP", "KDE:GNOME");

	const Environment environment = currentEnvironment();
	CHECK(environment.configHome == QDir::homePath() % QStringLiteral("/.config"));
	CHECK(environment.configDirs == QStringList{ QStringLiteral("/etc/xdg") });
	CHECK(environment.dataHome == QStringLiteral("/home data"));
	CHECK(environment.dataDirs == QStringList{ QStringLiteral("/first"), QStringLiteral("/second") });
	CHECK(environment.desktops == QStringList{ QStringLiteral("kde"), QStringLiteral("gnome") });
}
#endif
