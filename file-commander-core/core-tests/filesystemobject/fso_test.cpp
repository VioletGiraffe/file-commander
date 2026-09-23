#include "cfilesystemobject.h"

#include "../test-utils/src/catch2_utils.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "filesystem_types.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <stdint.h>
#include <string_view>

// Every object here is built in memory. Only the macOS bundle check reads the disk, under a parent that never exists.

[[nodiscard]] static QString parentPath()
{
	return QDir::rootPath() + QStringLiteral("fso-test-parent/");
}

[[nodiscard]] static thin_io::entry_status statusOf(const thin_io::entry_kind kind)
{
	thin_io::entry_status status;
	status.attributes.kind = kind;
	return status;
}

[[nodiscard]] static thin_io::directory_entry entryNamed(const std::string_view name, const thin_io::entry_status& status)
{
	thin_io::directory_entry entry;
	static_cast<thin_io::entry_status&>(entry) = status;
	entry.name.assign(name.begin(), name.end());
	return entry;
}

[[nodiscard]] static thin_io::directory_entry fileNamed(const std::string_view name)
{
	return entryNamed(name, statusOf(thin_io::entry_kind::regular_file));
}

// The link entry itself, as the platform lists it, with no target resolved
[[nodiscard]] static thin_io::directory_entry linkNamed(const std::string_view name, const bool toDirectory)
{
	thin_io::entry_status own;
	own.attributes.is_link = true;
#ifdef _WIN32
	own.attributes.kind = toDirectory ? thin_io::entry_kind::directory : thin_io::entry_kind::regular_file;
	own.attributes.reparse_tag = toDirectory ? IO_REPARSE_TAG_MOUNT_POINT : IO_REPARSE_TAG_SYMLINK;
#else
	(void)toDirectory;
	own.attributes.kind = thin_io::entry_kind::other;
	own.permissions = thin_io::file_permissions{ .mode = 0777 };
#endif
	return entryNamed(name, own);
}

[[nodiscard]] static CFileSystemObject listed(const thin_io::directory_entry& entry)
{
	return CFileSystemObject{ parentPath(), entry };
}

TEST_CASE("A listed file reports its entry", "[CFileSystemObject]")
{
	thin_io::directory_entry entry = fileNamed("report.final.txt");
	entry.logical_size = 12345;
	entry.times.creation = thin_io::timestamp{ .seconds = 100, .nanoseconds = 7 };
	entry.times.last_write = thin_io::timestamp{ .seconds = 200, .nanoseconds = 7 };

	const CFileSystemObject file = listed(entry);
	CHECK(file.type() == File);
	CHECK(file.exists());
	CHECK_FALSE(file.isLink());
	CHECK(file.fullAbsolutePath() == parentPath() + "report.final.txt");
	CHECK(file.parentDirPath() == parentPath());
	CHECK(file.fullName() == "report.final.txt");
	CHECK(file.name() == "report.final");
	CHECK(file.extension() == "txt");
	CHECK(file.size() == 12345u);
	CHECK(file.creationTime() == 100);
	CHECK(file.modificationTime() == 200);
	CHECK(file.hash() == pathHash(parentPath() + "report.final.txt"));
	CHECK_FALSE(file.isCdUp());
}

TEST_CASE("A listed directory's path ends with a separator and its name is whole", "[CFileSystemObject]")
{
	const CFileSystemObject directory = listed(entryNamed("txt.files", statusOf(thin_io::entry_kind::directory)));
	CHECK(directory.type() == Directory);
	CHECK(directory.isDir());
	CHECK(directory.fullAbsolutePath() == parentPath() + "txt.files/");
	CHECK(directory.name() == "txt.files");
	CHECK(directory.extension().isEmpty());
	CHECK(directory.size() == 0u);
	CHECK(directory.hash() == pathHash(parentPath() + "txt.files/"));
}

TEST_CASE("An unset time is reported as 0", "[CFileSystemObject]")
{
	const CFileSystemObject file = listed(fileNamed("file.bin"));
	CHECK(file.creationTime() == 0);
	CHECK(file.modificationTime() == 0);
	CHECK(file.size() == 0u); // No size reported
}

TEST_CASE("A file's extension follows its last dot", "[CFileSystemObject]")
{
	struct Split
	{
		std::string_view fullName;
		const char* name;
		const char* extension;
	};

	static constexpr Split splits[]{
		{ "archive.tar.gz", "archive.tar", "gz" },
		{ "noextension", "noextension", "" },
		{ "name.", "name.", "" }, // A trailing dot starts no extension
		{ ".config.json", ".config", "json" },
#ifdef _WIN32
		{ ".jpg", "", "jpg" }, // Windows software treats it as a JPG file
#else
		{ ".bashrc", ".bashrc", "" }, // A leading dot belongs to the name
#endif
	};

	for (const Split& split : splits)
	{
		const CFileSystemObject file = listed(fileNamed(split.fullName));
		INFO(split.fullName);
		CHECK(file.name() == split.name);
		CHECK(file.extension() == split.extension);
		CHECK(file.fullName() == QLatin1StringView{ split.fullName.data(), static_cast<qsizetype>(split.fullName.size()) });
	}
}

TEST_CASE("Hidden entries follow the platform's rule", "[CFileSystemObject]")
{
	thin_io::directory_entry flagged = fileNamed("flagged.txt");
	flagged.attributes.hidden = true;
	CHECK(listed(flagged).isHidden()); // Windows FILE_ATTRIBUTE_HIDDEN, or UF_HIDDEN
	CHECK_FALSE(listed(fileNamed("visible.txt")).isHidden());

#ifdef _WIN32
	CHECK_FALSE(listed(fileNamed(".dotfile")).isHidden());
#else
	CHECK(listed(fileNamed(".dotfile")).isHidden());
	CHECK(listed(entryNamed(".dotfolder", statusOf(thin_io::entry_kind::directory))).isHidden());
#endif
}

TEST_CASE("Executable files follow the platform's rule", "[CFileSystemObject]")
{
#ifdef _WIN32
	for (const std::string_view name : { "tool.exe", "TOOL.EXE", "command.com", "script.bat", "script.cmd" })
	{
		INFO(name);
		CHECK(listed(fileNamed(name)).isExecutable());
	}

	for (const std::string_view name : { "shortcut.pif", "notes.txt", "exe" })
	{
		INFO(name);
		CHECK_FALSE(listed(fileNamed(name)).isExecutable());
	}

	CHECK_FALSE(listed(entryNamed("folder.exe", statusOf(thin_io::entry_kind::directory))).isExecutable());
#else
	const auto withMode = [](const uint32_t mode) {
		thin_io::directory_entry entry = fileNamed("tool");
		entry.permissions = thin_io::file_permissions{ .mode = mode };
		return listed(entry);
	};

	CHECK(withMode(0755).isExecutable());
	CHECK(withMode(0100).isExecutable());
	CHECK(withMode(0001).isExecutable());
	CHECK_FALSE(withMode(0644).isExecutable());
	CHECK_FALSE(listed(fileNamed("tool")).isExecutable()); // No permissions reported
#endif
}

TEST_CASE("A live link is described by its target", "[CFileSystemObject]")
{
	thin_io::entry_status targetFile = statusOf(thin_io::entry_kind::regular_file);
	targetFile.logical_size = 5;
	targetFile.times.last_write = thin_io::timestamp{ .seconds = 300 };
#ifndef _WIN32
	targetFile.permissions = thin_io::file_permissions{ .mode = 0644 };
#endif

	thin_io::directory_entry fileLink = linkNamed("file-link", false);
	fileLink.link_target = targetFile;
	const CFileSystemObject file = listed(fileLink);
	CHECK(file.isLink());
	CHECK(file.type() == File);
	CHECK(file.size() == 5u);
	CHECK(file.modificationTime() == 300);
	CHECK(file.fullAbsolutePath() == parentPath() + "file-link");
	CHECK_FALSE(file.isExecutable()); // The target's mode, not the link's own

	thin_io::directory_entry directoryLink = linkNamed("folder-link", true);
	directoryLink.link_target = statusOf(thin_io::entry_kind::directory);
	const CFileSystemObject directory = listed(directoryLink);
	CHECK(directory.isLink());
	CHECK(directory.type() == Directory);
	CHECK(directory.fullAbsolutePath() == parentPath() + "folder-link/");
	CHECK(directory.name() == "folder-link");
}

TEST_CASE("A link to neither a file nor a directory is shown as a file", "[CFileSystemObject]")
{
	thin_io::directory_entry link = linkNamed("socket-link", false);
	link.link_target = statusOf(thin_io::entry_kind::other);
	CHECK(listed(link).type() == File);
}

TEST_CASE("A broken link keeps its own kind and grants nothing", "[CFileSystemObject]")
{
	const CFileSystemObject brokenFileLink = listed(linkNamed("file-link", false));
	CHECK(brokenFileLink.isLink());
	CHECK(brokenFileLink.exists());
	CHECK(brokenFileLink.type() == File);
	CHECK(brokenFileLink.size() == 0u);
	CHECK_FALSE(brokenFileLink.isExecutable()); // A POSIX link's own mode is 0777

	const CFileSystemObject brokenDirectoryLink = listed(linkNamed("folder-link", true));
	CHECK(brokenDirectoryLink.isLink());
#ifdef _WIN32
	CHECK(brokenDirectoryLink.type() == Directory); // A junction is a directory entry itself
	CHECK(brokenDirectoryLink.fullAbsolutePath() == parentPath() + "folder-link/");
#else
	CHECK(brokenDirectoryLink.type() == File); // A POSIX symlink is neither
	CHECK(brokenDirectoryLink.fullAbsolutePath() == parentPath() + "folder-link");
#endif
}

#ifdef _WIN32
TEST_CASE("A reparse point that is not a name surrogate is a plain entry", "[CFileSystemObject]")
{
	thin_io::directory_entry compressed = fileNamed("compressed.dll");
	compressed.attributes.is_link = true;
	compressed.attributes.reparse_tag = IO_REPARSE_TAG_WOF;
	compressed.logical_size = 10;

	const CFileSystemObject file = listed(compressed);
	CHECK_FALSE(file.isLink());
	CHECK(file.type() == File);
	CHECK(file.size() == 10u);
}
#endif

TEST_CASE("An entry that is neither a file nor a directory is of unknown type", "[CFileSystemObject]")
{
	const CFileSystemObject socket = listed(entryNamed("socket", statusOf(thin_io::entry_kind::other)));
	CHECK(socket.type() == UnknownType);
	CHECK_FALSE(socket.isFile());
	CHECK_FALSE(socket.isDir());
}

TEST_CASE("The [..] entry is the parent folder, and a root has none", "[CFileSystemObject]")
{
	const QString folder = parentPath() + "folder/";
	const auto cdUp = CFileSystemObject::cdUpEntryOf(folder);
	REQUIRE(cdUp);
	CHECK(cdUp->isCdUp());
	CHECK(cdUp->fullName() == "..");
	CHECK(cdUp->name() == "..");
	CHECK(cdUp->extension().isEmpty());
	CHECK(cdUp->exists());
	CHECK(cdUp->type() == Directory);
	CHECK(cdUp->fullAbsolutePath() == parentPath());
	CHECK(cdUp->hash() == pathHash(parentPath()));

	// Directly under a root
	const auto rootCdUp = CFileSystemObject::cdUpEntryOf(parentPath());
	REQUIRE(rootCdUp);
	CHECK(rootCdUp->fullAbsolutePath() == QDir::rootPath());

	CHECK_FALSE(CFileSystemObject::cdUpEntryOf(QDir::rootPath()));
}

TEST_CASE("An object built from its properties reports all of them", "[CFileSystemObject]")
{
	CFileSystemObjectProperties properties;
	properties.fullPath = parentPath() + "tool.exe";
	properties.type = File;
	properties.exists = true;
	properties.isHidden = true;
	properties.isExecutable = true;
	properties.hash = 1; // Replaced by the path's hash

	const CFileSystemObject object{ properties };
	CHECK(object.fullName() == "tool.exe");
	CHECK(object.name() == "tool");
	CHECK(object.extension() == "exe");
	CHECK(object.isHidden());
	CHECK(object.isExecutable());
	CHECK(object.hash() == pathHash(properties.fullPath));
}

TEST_CASE("An object from an empty path is empty", "[CFileSystemObject]")
{
	const CFileSystemObject fso{ QString{} };

	SECTION_WITH_AUTO_NAME {
		CHECK(fso == CFileSystemObject{});
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(!fso.exists());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.extension().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.fullAbsolutePath().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.fullName().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.hash() == 0);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isCdUp() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isDir() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isExecutable() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isFile() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isHidden() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.isValid() == false);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.name().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.parentDirPath().isEmpty());
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.size() == 0);
	}

	SECTION_WITH_AUTO_NAME {
		CHECK(fso.type() == UnknownType);
	}
}
