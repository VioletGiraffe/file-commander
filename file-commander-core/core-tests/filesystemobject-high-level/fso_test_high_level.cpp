#include "cfilesystemobject.h"
#include "directoryscanner.h"
#include "filesystemhelperfunctions.h" // toNativeSeparators

#include "link_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "file.hpp" // thin_io


#define CATCH_CONFIG_MAIN
DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include <algorithm>

[[nodiscard]] static const CFileSystemObject* findByFullName(const FileListHashMap& items, const QString& fullName)
{
	const auto found = std::find_if(items.begin(), items.end(), [&fullName](const auto& item) { return item.second.fullName() == fullName; });
	return found == items.end() ? nullptr : &found->second;
}

// Win32 path parsing strips trailing dots and spaces; thin_io keeps them in an absolute path
[[nodiscard]] static bool createFileVerbatim(const QString& path)
{
	thin_io::file file;
#ifdef _WIN32
	return file.open(reinterpret_cast<const wchar_t*>(path.utf16()), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew);
#else
	return file.open(QFile::encodeName(path).constData(), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew);
#endif
}

[[nodiscard]] static bool deleteFileVerbatim(const QString& path)
{
#ifdef _WIN32
	return thin_io::file::delete_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
	return thin_io::file::delete_file(QFile::encodeName(path).constData());
#endif
}

TEST_CASE("::pathHierarchy tests", "[CFileSystemObject]")
{
	CHECK(::pathHierarchy( {} ).empty());

	{
		const auto hierarchy = ::pathHierarchy(".");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "."));
	}

	{
		const auto hierarchy = ::pathHierarchy("..");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == ".."));
	}

	{
		const auto hierarchy = ::pathHierarchy("/");
		CHECK((hierarchy.size() == 1 && hierarchy.front() == "/"));
	}

#ifndef _WIN32
	{
		const auto hierarchy = ::pathHierarchy("/Users/admin/Downloads/1.txt");
		const std::vector<QString> reference{"/Users/admin/Downloads/1.txt", "/Users/admin/Downloads/", "/Users/admin/", "/Users/", "/"};
		CHECK(hierarchy == reference);
	}
#else
	{
		const auto hierarchy = ::pathHierarchy("R:/Docs/1/2/3/txt.files/important.document.txt");
		const std::vector<QString> reference{ "R:/Docs/1/2/3/txt.files/important.document.txt", "R:/Docs/1/2/3/txt.files/", "R:/Docs/1/2/3/", "R:/Docs/1/2/", "R:/Docs/1/", "R:/Docs/", "R:/"};
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = ::pathHierarchy("R:");
		const std::vector<QString> reference{ "R:" };
		CHECK(hierarchy == reference);
	}

	{
		const auto hierarchy = ::pathHierarchy("R:/");
		const std::vector<QString> reference{ "R:/" };
		CHECK(hierarchy == reference);
	}
#endif
}

TEST_CASE("rootFileSystemId for a non-existent path", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());

	const CFileSystemObject existingDir{ tempDir.path() };
	REQUIRE(existingDir.exists());

	const CFileSystemObject nonExistentChild{ tempDir.path() + "/does_not_exist/child.txt" };
	REQUIRE(!nonExistentChild.exists());

	// Regression: rootFileSystemId() used to read uninitialized memory for a non-existent path and return a garbage
	// device ID. It must resolve to the device of the nearest existing ancestor.
	CHECK(existingDir.rootFileSystemId() == nonExistentChild.rootFileSystemId());
}

// The trailing separator on a directory's path is load-bearing: it is what makes "dir" and "dir/" hash and compare
// equal, it is how a non-existent path gets classified at all, and parentDirPath() preserves it.
TEST_CASE("A directory's path always ends with a separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/folder";
	REQUIRE(QDir{}.mkpath(folder));

	const CFileSystemObject withoutSeparator{ folder };
	const CFileSystemObject withSeparator{ folder + "/" };

	REQUIRE(withoutSeparator.isDir());
	CHECK(withoutSeparator.fullAbsolutePath() == folder + "/");
	CHECK(withSeparator.fullAbsolutePath() == folder + "/");

	// The point of the normalization: either spelling names the same object.
	CHECK(withoutSeparator.hash() == withSeparator.hash());
	CHECK(withoutSeparator == withSeparator);

	// The native spelling round-trips, which is what lets a path leave through currentDirPathNative() and come back.
	CHECK(CFileSystemObject{ toNativeSeparators(withoutSeparator.fullAbsolutePath()) } == withoutSeparator);

	const QString filePath = folder + "/file.txt";
	{
		QFile file{ filePath };
		REQUIRE(file.open(QFile::WriteOnly));
	}

	const CFileSystemObject fileObject{ filePath };
	REQUIRE(fileObject.isFile());
	CHECK(fileObject.fullAbsolutePath() == filePath); // Only directories carry the separator
	CHECK(fileObject.parentDirPath() == folder + "/");
}

// Nothing on disk to inspect means the spelling is the only classification available. This is what lets a copy or move
// destination be recognized as a folder before it exists.
TEST_CASE("A non-existent path is a directory exactly when spelled with a separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString missing = tempDir.path() + "/not_created";

	const CFileSystemObject asDirectory{ missing + "/" };
	REQUIRE(!asDirectory.exists());
	CHECK(asDirectory.isDir());
	CHECK(asDirectory.type() == Directory);
	CHECK(asDirectory.fullAbsolutePath() == missing + "/");

	const CFileSystemObject asEntry{ missing };
	REQUIRE(!asEntry.exists());
	CHECK(!asEntry.isDir());
	CHECK(!asEntry.isFile());
	CHECK(asEntry.fullAbsolutePath() == missing);
}

// Duplicate separators and "."/".." resolve before the trailing separator is applied, so a directory still comes out
// with exactly one.
TEST_CASE("Path normalization precedes the trailing separator", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/folder";
	REQUIRE(QDir{}.mkpath(folder));

	CHECK(CFileSystemObject{ folder + "//" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ tempDir.path() + "//folder" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ folder + "/." }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ folder + "/../folder" }.fullAbsolutePath() == folder + "/");
	CHECK(CFileSystemObject{ QDir::toNativeSeparators(folder) }.fullAbsolutePath() == folder + "/");
}

TEST_CASE("A dotted directory name is reported whole", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/txt.files";
	REQUIRE(QDir{}.mkpath(folder));

	for (const CFileSystemObject& object : { CFileSystemObject{ folder }, CFileSystemObject{ folder + "/" } })
	{
		CHECK(object.fullName() == "txt.files");
		CHECK(object.extension().isEmpty()); // A directory has no extension, however its name is spelled
	}
}

// A live link is classified - and separator-normalized - as the directory it points at.
// A dead one stays a deletable entry classified by its own kind: a Windows junction is a directory entry, a POSIX symlink is not.
TEST_CASE("A directory link is a directory until its target is gone", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString target = tempDir.path() + "/target";
	const QString link = tempDir.path() + "/link";
	REQUIRE(QDir{}.mkpath(target));
	REQUIRE(createDirectoryLink(target, link));

	{
		const CFileSystemObject linkObject{ link };
		REQUIRE(linkObject.exists());
		CHECK(linkObject.isLink());
		CHECK(linkObject.isDir());
		CHECK(linkObject.type() == Directory);
		CHECK(linkObject.fullAbsolutePath() == link + "/"); // Followed, so it is separator-normalized like any directory
		CHECK(CFileSystemObject{ link + "/" } == linkObject);
	}

	REQUIRE(QDir{ target }.removeRecursively());

	const CFileSystemObject brokenLink{ link };
	CHECK(brokenLink.isLink());
	CHECK(brokenLink.exists()); // A link entry exists even when its target does not

#ifdef _WIN32
	CHECK(brokenLink.isDir());
	CHECK(brokenLink.type() == Directory);
	CHECK(brokenLink.fullAbsolutePath() == link + "/");
#else
	CHECK(!brokenLink.isDir());
	CHECK(brokenLink.type() == File);
	CHECK(brokenLink.fullAbsolutePath() == link);
#endif
}

TEST_CASE("A filesystem root is its own path and has no parent", "[CFileSystemObject]")
{
	const CFileSystemObject root{ QDir::rootPath() };
	REQUIRE(root.isDir());
	CHECK(root.fullAbsolutePath().endsWith('/'));
	CHECK(root.parentDirPath().isEmpty()); // What stops navigateUp() at the top of a volume
	CHECK_FALSE(root.isHidden()); // A Windows drive root reports the hidden attribute

#ifdef _WIN32
	// The drive letter is canonically uppercase, so either spelling hashes to the same object.
	CHECK(CFileSystemObject{ QStringLiteral("c:/") } == CFileSystemObject{ QStringLiteral("C:/") });
#endif
}

TEST_CASE("File times are read once, when the object is built", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString filePath = tempDir.path() + "/file.txt";
	const QString folderPath = tempDir.path() + "/folder";
	REQUIRE(QDir{}.mkpath(folderPath));

	const auto setModificationTime = [&filePath](const QDateTime& time) {
		QFile file{ filePath };
		return file.open(QFile::Append) && file.setFileTime(time, QFileDevice::FileModificationTime);
	};

	const QDateTime firstTime{ QDate{ 2001, 2, 3 }, QTime{ 4, 5, 6 }, QTimeZone::UTC };
	const QDateTime secondTime{ QDate{ 2011, 12, 13 }, QTime{ 14, 15, 16 }, QTimeZone::UTC };
	REQUIRE(setModificationTime(firstTime));

	const CFileSystemObject fileObject{ filePath };
	CHECK(fileObject.modificationTime() == firstTime.toSecsSinceEpoch());
	CHECK(fileObject.creationTime() == QFileInfo{ filePath }.birthTime(QTimeZone::UTC).toSecsSinceEpoch()); // 0 where the filesystem has none

	REQUIRE(setModificationTime(secondTime));
	CHECK(fileObject.modificationTime() == firstTime.toSecsSinceEpoch());
	CHECK(CFileSystemObject{ filePath }.modificationTime() == secondTime.toSecsSinceEpoch());

	const FileListHashMap listed = listDirectoryForPanel(tempDir.path() + '/', true);
	const CFileSystemObject* listedFolder = findByFullName(listed, "folder");
	const CFileSystemObject* listedFile = findByFullName(listed, "file.txt");
	REQUIRE(listedFolder);
	REQUIRE(listedFile);
	REQUIRE(listedFolder->isDir());
	CHECK(listedFolder->modificationTime() == QFileInfo{ folderPath }.lastModified(QTimeZone::UTC).toSecsSinceEpoch());
	CHECK(listedFile->modificationTime() == secondTime.toSecsSinceEpoch());
	CHECK(listedFile->creationTime() == fileObject.creationTime());
}

// Navigation and cursor restore key on the [..] entry's hash
TEST_CASE("The panel listing's [..] entry is the parent folder", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + "/folder/";
	REQUIRE(QDir{}.mkpath(folder));

	for (const bool showHiddenFiles : { true, false }) // On POSIX, ".." looks hidden
	{
		const FileListHashMap items = listDirectoryForPanel(folder, showHiddenFiles);
		const CFileSystemObject* cdUp = findByFullName(items, "..");
		REQUIRE(cdUp);
		CHECK(cdUp->isCdUp());
		CHECK(cdUp->fullAbsolutePath() == tempDir.path() + "/");
		CHECK(cdUp->hash() == CFileSystemObject{ tempDir.path() }.hash());
		CHECK(cdUp->type() == Directory);
	}

	CHECK_FALSE(findByFullName(listDirectoryForPanel(QDir::rootPath(), true), ".."));
}

TEST_CASE("A folder directly under a root has the root as its parent", "[CFileSystemObject]")
{
	CFileSystemObjectProperties properties;
	properties.fullPath = QDir::rootPath() + "folder/";
	properties.type = Directory;
	CHECK(CFileSystemObject{ properties }.parentDirPath() == QDir::rootPath());
}

TEST_CASE("Names ending in a dot or a space are kept verbatim", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
	const QString dotted = folder + "name.";
	const QString spaced = folder + "name ";
	REQUIRE(createFileVerbatim(dotted));
	REQUIRE(createFileVerbatim(spaced));

	const FileListHashMap items = listDirectoryForPanel(folder, true);
	for (const QString& path : { dotted, spaced })
	{
		const CFileSystemObject object{ path };
		REQUIRE(object.isFile());
		CHECK(object.fullAbsolutePath() == path);
		CHECK(items.contains(object.hash()));
	}

	// A trailing dot starts no extension
	CHECK(CFileSystemObject{ dotted }.name() == "name.");
	CHECK(CFileSystemObject{ dotted }.extension().isEmpty());

	// QTemporaryDir deletes through Win32 path parsing, which cannot address these
	CHECK(deleteFileVerbatim(dotted));
	CHECK(deleteFileVerbatim(spaced));
}

TEST_CASE("A file's extension follows its last dot, except a leading one on POSIX", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
	REQUIRE(createFileVerbatim(folder + ".jpg"));
	REQUIRE(createFileVerbatim(folder + "archive.tar.gz"));

	const FileListHashMap items = listDirectoryForPanel(folder, true);
	const CFileSystemObject dotfileFromPath{ folder + ".jpg" };
	for (const CFileSystemObject* dotfile : { findByFullName(items, ".jpg"), &dotfileFromPath })
	{
		REQUIRE(dotfile);
#ifdef _WIN32
		CHECK(dotfile->name().isEmpty());
		CHECK(dotfile->extension() == "jpg");
#else
		CHECK(dotfile->name() == ".jpg");
		CHECK(dotfile->extension().isEmpty());
#endif
	}

	const CFileSystemObject* archive = findByFullName(items, "archive.tar.gz");
	REQUIRE(archive);
	CHECK(archive->name() == "archive.tar");
	CHECK(archive->extension() == "gz");
}

TEST_CASE("Hidden entries follow the platform's rule", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
	const QString visible = folder + "visible.txt";
#ifdef _WIN32
	const QString hidden = folder + "hidden.txt";
#else
	const QString hidden = folder + ".hidden.txt";
#endif
	REQUIRE(createFileVerbatim(visible));
	REQUIRE(createFileVerbatim(hidden));
#ifdef _WIN32
	REQUIRE(::SetFileAttributesW(reinterpret_cast<const wchar_t*>(hidden.utf16()), FILE_ATTRIBUTE_HIDDEN) != FALSE);
#endif

	CHECK(CFileSystemObject{ hidden }.isHidden());
	CHECK_FALSE(CFileSystemObject{ visible }.isHidden());

	const FileListHashMap withoutHidden = listDirectoryForPanel(folder, false);
	CHECK(findByFullName(withoutHidden, "visible.txt"));
	CHECK_FALSE(findByFullName(withoutHidden, CFileSystemObject{ hidden }.fullName()));

	const FileListHashMap withHidden = listDirectoryForPanel(folder, true);
	const CFileSystemObject* listedHidden = findByFullName(withHidden, CFileSystemObject{ hidden }.fullName());
	REQUIRE(listedHidden);
	CHECK(listedHidden->isHidden());
}

TEST_CASE("Executable files follow the platform's rule", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
#ifdef _WIN32
	const QStringList executables{ "tool.EXE", "script.cmd" };
#else
	const QStringList executables{ "tool" };
#endif
	for (const QString& name : executables)
	{
		REQUIRE(createFileVerbatim(folder + name));
#ifndef _WIN32
		REQUIRE(QFile::setPermissions(folder + name, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
#endif
	}
	REQUIRE(createFileVerbatim(folder + "notes.txt"));

	const FileListHashMap items = listDirectoryForPanel(folder, true);
	for (const QString& name : executables)
	{
		CHECK(CFileSystemObject{ folder + name }.isExecutable());
		REQUIRE(findByFullName(items, name));
		CHECK(findByFullName(items, name)->isExecutable());
	}

	CHECK_FALSE(CFileSystemObject{ folder + "notes.txt" }.isExecutable());
	REQUIRE(findByFullName(items, "notes.txt"));
	CHECK_FALSE(findByFullName(items, "notes.txt")->isExecutable());
}

TEST_CASE("A listed link is described by its target until the target is gone", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
	const QString targetFolder = folder + "target";
	const QString targetFile = folder + "target.bin";
	REQUIRE(QDir{}.mkpath(targetFolder));
	{
		QFile file{ targetFile };
		REQUIRE(file.open(QFile::WriteOnly));
		REQUIRE(file.write("12345") == 5);
	}
	REQUIRE(createDirectoryLink(targetFolder, folder + "folder-link"));
	// A symlink needs elevation or Developer Mode on Windows
	const bool fileLinkCreated = createFileSymlink(targetFile, folder + "file-link");

	{
		const FileListHashMap items = listDirectoryForPanel(folder, true);
		const CFileSystemObject* folderLink = findByFullName(items, "folder-link");
		REQUIRE(folderLink);
		CHECK(folderLink->isLink());
		CHECK(folderLink->type() == Directory);
		CHECK(folderLink->fullAbsolutePath() == folder + "folder-link/");

		if (fileLinkCreated)
		{
			const CFileSystemObject* fileLink = findByFullName(items, "file-link");
			REQUIRE(fileLink);
			CHECK(fileLink->isLink());
			CHECK(fileLink->type() == File);
			CHECK(fileLink->size() == 5);
		}
		else
			WARN("No file symlink: the process may not create one");
	}

	REQUIRE(QDir{ targetFolder }.removeRecursively());
	REQUIRE(QFile::remove(targetFile));

	const FileListHashMap items = listDirectoryForPanel(folder, true);
	const CFileSystemObject* brokenFolderLink = findByFullName(items, "folder-link");
	REQUIRE(brokenFolderLink);
	CHECK(brokenFolderLink->isLink());
	CHECK(brokenFolderLink->exists());
#ifdef _WIN32
	CHECK(brokenFolderLink->type() == Directory); // A junction is a directory entry itself
#else
	CHECK(brokenFolderLink->type() == File);
#endif

	if (fileLinkCreated)
	{
		const CFileSystemObject* brokenFileLink = findByFullName(items, "file-link");
		REQUIRE(brokenFileLink);
		CHECK(brokenFileLink->isLink());
		CHECK(brokenFileLink->type() == File);
		CHECK(brokenFileLink->size() == 0);
	}
}

#ifndef _WIN32
TEST_CASE("A socket is left out of the panel listing", "[CFileSystemObject][listing]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString folder = tempDir.path() + '/';
	const QByteArray socketPath = QFile::encodeName(folder + "socket");

	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	REQUIRE(static_cast<size_t>(socketPath.size()) < sizeof(address.sun_path));
	std::copy(socketPath.begin(), socketPath.end(), address.sun_path);

	const int socketFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	REQUIRE(socketFd >= 0);
	const bool bound = ::bind(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
	::close(socketFd);
	REQUIRE(bound);

	const CFileSystemObject socketObject{ folder + "socket" };
	CHECK(socketObject.exists());
	CHECK_FALSE(socketObject.isFile());
	CHECK_FALSE(socketObject.isDir());
	CHECK_FALSE(findByFullName(listDirectoryForPanel(folder, true), "socket"));
}
#endif

TEST_CASE("An object built from its properties reports them", "[CFileSystemObject]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());

	CFileSystemObjectProperties fileProperties;
	fileProperties.fullPath = tempDir.path() + "/report.final.txt"; // Never created
	fileProperties.fullName = "report.final.txt";
	fileProperties.completeBaseName = "report.final";
	fileProperties.extension = "txt";
	fileProperties.type = File;
	fileProperties.exists = true;
	fileProperties.size = 12345u;
	fileProperties.creationTime = 100;
	fileProperties.modificationTime = 200;
	fileProperties.hash = 1; // Replaced by the path's hash

	const CFileSystemObject file{ fileProperties };
	CHECK(file.isFile());
	CHECK(file.exists());
	CHECK(file.fullAbsolutePath() == fileProperties.fullPath);
	CHECK(file.fullName() == "report.final.txt");
	CHECK(file.name() == "report.final");
	CHECK(file.extension() == "txt");
	CHECK(file.size() == 12345u);
	CHECK(file.creationTime() == 100);
	CHECK(file.modificationTime() == 200);
	CHECK(file.hash() == CFileSystemObject{ fileProperties.fullPath }.hash());

	CFileSystemObjectProperties folderProperties;
	folderProperties.fullPath = tempDir.path() + "/folder/";
	folderProperties.fullName = "folder";
	folderProperties.completeBaseName = "folder";
	folderProperties.type = Directory;
	folderProperties.exists = true;

	const CFileSystemObject folder{ folderProperties };
	CHECK(folder.isDir());
	CHECK(folder.hash() == CFileSystemObject{ folderProperties.fullPath }.hash());
	CHECK(CFileSystemObject{ CFileSystemObjectProperties{} }.hash() == 0u);
}

