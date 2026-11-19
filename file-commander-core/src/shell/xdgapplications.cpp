#include "xdgapplications.h"

#include "cshell.h"
#include "filesystemhelperfunctions.h"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSet>
#include <QStandardPaths>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <utility>

using namespace XdgApplications;

namespace {

using KeyGroup = QHash<QString, QString>; // Key -> raw value

struct InstalledEntry
{
	Application application;
	QStringList mimeTypes;
};

// One mimeapps.list or defaults.list: MIME type -> desktop file IDs
struct Associations
{
	QHash<QString, QStringList> defaults, added, removed;
};

struct AssociationDir
{
	std::vector<Associations> files; // Most important first
	std::vector<size_t> entries; // An applications folder's own desktop entries, sorted by ID; none for a config folder
};

}

// Only absolute paths count: the spec has relative ones ignored
static QString absolutePathVariable(const char* variable, const QString& fallback)
{
	const QString path = qEnvironmentVariable(variable);
	return QDir::isAbsolutePath(path) ? path : fallback;
}

static QStringList absolutePathListVariable(const char* variable, const QStringList& fallback)
{
	QStringList paths = qEnvironmentVariable(variable).split(':', Qt::SkipEmptyParts);
	paths.removeIf([](const QString& path) { return !QDir::isAbsolutePath(path); });
	return !paths.isEmpty() ? paths : fallback;
}

Environment XdgApplications::currentEnvironment()
{
	const QString home = QDir::homePath();
	return {
		.configHome = absolutePathVariable("XDG_CONFIG_HOME", home + QStringLiteral("/.config")),
		.configDirs = absolutePathListVariable("XDG_CONFIG_DIRS", { QStringLiteral("/etc/xdg") }),
		.dataHome = absolutePathVariable("XDG_DATA_HOME", home + QStringLiteral("/.local/share")),
		.dataDirs = absolutePathListVariable("XDG_DATA_DIRS", { QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share") }),
		.desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower().split(':', Qt::SkipEmptyParts),
		.localeName = QLocale::system().name(),
	};
}

// Group name -> its keys. Empty when the file cannot be read.
static std::map<QString, KeyGroup> readKeyFile(const QString& path)
{
	QFile file{ path };
	if (!file.open(QIODevice::ReadOnly))
		return {};

	std::map<QString, KeyGroup> groups;
	KeyGroup* group = nullptr; // Null before the first group header: keys there belong to no group
	for (const QString& line : QString::fromUtf8(file.readAll()).split('\n'))
	{
		const QStringView trimmed = QStringView{ line }.trimmed();
		if (trimmed.isEmpty() || trimmed.startsWith('#'))
			continue;

		if (trimmed.startsWith('[') && trimmed.endsWith(']'))
			group = &groups[trimmed.sliced(1, trimmed.size() - 2).toString()];
		else if (const qsizetype equals = trimmed.indexOf('='); group && equals > 0)
			group->insert(trimmed.first(equals).trimmed().toString(), trimmed.sliced(equals + 1).trimmed().toString());
	}

	return groups;
}

static std::optional<QChar> unescapedChar(const QChar escaped)
{
	switch (escaped.unicode())
	{
	case 's': return QChar{ ' ' };
	case 'n': return QChar{ '\n' };
	case 't': return QChar{ '\t' };
	case 'r': return QChar{ '\r' };
	case '\\': return QChar{ '\\' };
	case ';': return QChar{ ';' }; // Only meaningful in a list
	default: return std::nullopt;
	}
}

// An unknown escape is kept as written
static QString unescapedString(const QStringView raw)
{
	QString value;
	value.reserve(raw.size());
	for (qsizetype i = 0; i < raw.size(); ++i)
	{
		if (raw[i] == '\\' && i + 1 < raw.size())
		{
			if (const std::optional<QChar> c = unescapedChar(raw[i + 1]))
			{
				value += *c;
				++i;
				continue;
			}
		}

		value += raw[i];
	}

	return value;
}

// A list value's entries, split at the semicolons that are not escaped. Empty entries are dropped.
static QStringList unescapedList(const QStringView raw)
{
	QStringList values;
	qsizetype start = 0;
	for (qsizetype i = 0; i <= raw.size(); ++i)
	{
		if (i + 1 < raw.size() && raw[i] == '\\')
		{
			++i; // The escaped character is no separator
			continue;
		}

		if (i == raw.size() || raw[i] == ';')
		{
			if (i > start)
				values.push_back(unescapedString(raw.sliced(start, i - start)));
			start = i + 1;
		}
	}

	return values;
}

static QString localizedValue(const KeyGroup& group, const QString& key, const QString& localeName)
{
	const QString language = localeName.section('_', 0, 0);
	for (const QString& localizedKey : { QString{ key % '[' % localeName % ']' }, QString{ key % '[' % language % ']' } })
	{
		if (const auto value = group.constFind(localizedKey); value != group.cend())
			return unescapedString(*value);
	}

	return unescapedString(group.value(key));
}

// Empty for an entry that is no application, is hidden, has no command line, or whose TryExec program is missing.
// NoDisplay does not exclude an entry: it hides the entry from menus, not from file associations.
static std::optional<InstalledEntry> readEntry(const QString& path, QString id, const QString& localeName)
{
	const std::map<QString, KeyGroup> groups = readKeyFile(path);
	const auto group = groups.find(QStringLiteral("Desktop Entry"));
	if (group == groups.end())
		return std::nullopt;

	const KeyGroup& keys = group->second;
	if (keys.value(QStringLiteral("Type")) != QStringLiteral("Application") || keys.value(QStringLiteral("Hidden")) == QStringLiteral("true"))
		return std::nullopt;

	QString exec = unescapedString(keys.value(QStringLiteral("Exec")));
	if (exec.isEmpty())
		return std::nullopt;

	if (const QString tryExec = unescapedString(keys.value(QStringLiteral("TryExec"))); !tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty())
		return std::nullopt;

	return InstalledEntry{
		.application = {
			.id = std::move(id),
			.desktopFilePath = path,
			.name = localizedValue(keys, QStringLiteral("Name"), localeName),
			.icon = localizedValue(keys, QStringLiteral("Icon"), localeName),
			.exec = std::move(exec),
			.workingDir = unescapedString(keys.value(QStringLiteral("Path"))),
			.terminal = keys.value(QStringLiteral("Terminal")) == QStringLiteral("true"),
		},
		.mimeTypes = unescapedList(keys.value(QStringLiteral("MimeType"))),
	};
}

static Associations readAssociations(const QString& path)
{
	const std::map<QString, KeyGroup> groups = readKeyFile(path);
	const auto lists = [&groups](const QString& groupName) {
		QHash<QString, QStringList> idsByType;
		if (const auto group = groups.find(groupName); group != groups.end())
		{
			for (auto key = group->second.cbegin(); key != group->second.cend(); ++key)
				idsByType.insert(key.key(), unescapedList(key.value()));
		}
		return idsByType;
	};

	return {
		.defaults = lists(QStringLiteral("Default Applications")),
		.added = lists(QStringLiteral("Added Associations")),
		.removed = lists(QStringLiteral("Removed Associations")),
	};
}

namespace {

// The installed applications and the associations of every folder, read once for a lookup
class Index
{
public:
	explicit Index(const Environment& environment)
	{
		QStringList applicationDirs{ environment.dataHome % QStringLiteral("/applications") };
		for (const QString& dataDir : environment.dataDirs)
			applicationDirs.push_back(dataDir % QStringLiteral("/applications"));

		std::vector<std::vector<size_t>> entriesByApplicationDir = readEntries(applicationDirs, environment.localeName);

		// The desktop-specific lists come before the generic one, in every folder
		QStringList listNames;
		for (const QString& desktop : environment.desktops)
			listNames.push_back(desktop % QStringLiteral("-mimeapps.list"));
		listNames.push_back(QStringLiteral("mimeapps.list"));

		for (const QString& configDir : QStringList{ environment.configHome } + environment.configDirs)
			_dirs.push_back({ .files = readAssociationFiles(configDir, listNames), .entries = {} });

		// defaults.list: the former name of a folder's default applications list
		listNames.push_back(QStringLiteral("defaults.list"));
		for (qsizetype i = 0; i < applicationDirs.size(); ++i)
			_dirs.push_back({ .files = readAssociationFiles(applicationDirs[i], listNames), .entries = std::move(entriesByApplicationDir[(size_t)i]) });
	}

	// The file's default application first, then the others; its own type's before its ancestors'
	[[nodiscard]] std::vector<size_t> applicationsForFileType(const QStringList& typeWithAncestors) const
	{
		std::vector<size_t> entries;
		const auto add = [&entries](const size_t entry) {
			if (std::ranges::find(entries, entry) == entries.end())
				entries.push_back(entry);
		};

		for (const QString& type : typeWithAncestors)
		{
			if (const std::optional<size_t> entry = defaultEntry(type))
			{
				add(*entry);
				break;
			}
		}

		for (const QString& type : typeWithAncestors)
		{
			for (const size_t entry : associatedEntries(type))
				add(entry);
		}

		return entries;
	}

	[[nodiscard]] const Application& application(const size_t entry) const
	{
		return _entries[entry].application;
	}

private:
	// Each folder's entries, in applicationDirs order
	[[nodiscard]] std::vector<std::vector<size_t>> readEntries(const QStringList& applicationDirs, const QString& localeName)
	{
		std::vector<std::vector<size_t>> entriesByDir;
		QSet<QString> seenIds; // A desktop file ID is the one in the most important folder, even when that entry is hidden
		for (const QString& applicationDir : applicationDirs)
		{
			std::vector<size_t>& dirEntries = entriesByDir.emplace_back();
			const QDir dir{ applicationDir };
			for (QDirIterator file{ applicationDir, { QStringLiteral("*.desktop") }, QDir::Files, QDirIterator::Subdirectories }; file.hasNext();)
			{
				const QString path = file.next();
				// A subfolder's name is a prefix of the ID: kde/kate.desktop is kde-kate.desktop
				QString id = dir.relativeFilePath(path).replace('/', '-');
				if (seenIds.contains(id))
					continue;

				seenIds.insert(id);
				if (std::optional<InstalledEntry> entry = readEntry(path, id, localeName))
				{
					_entryById.insert(id, _entries.size());
					dirEntries.push_back(_entries.size());
					_entries.push_back(std::move(*entry));
				}
			}

			// The iteration order is arbitrary
			std::ranges::sort(dirEntries, {}, [this](const size_t entry) -> const QString& { return _entries[entry].application.id; });
		}

		return entriesByDir;
	}

	[[nodiscard]] static std::vector<Associations> readAssociationFiles(const QString& dir, const QStringList& listNames)
	{
		std::vector<Associations> files;
		for (const QString& listName : listNames)
			files.push_back(readAssociations(dir % '/' % listName));
		return files;
	}

	[[nodiscard]] std::optional<size_t> installedEntry(const QString& id) const
	{
		const auto entry = _entryById.constFind(id);
		return entry != _entryById.cend() ? std::optional{ *entry } : std::nullopt;
	}

	// The first installed application a default list names for `type`
	[[nodiscard]] std::optional<size_t> defaultEntry(const QString& type) const
	{
		for (const AssociationDir& dir : _dirs)
		{
			for (const Associations& file : dir.files)
			{
				for (const QString& id : file.defaults.value(type))
				{
					if (const std::optional<size_t> entry = installedEntry(id))
						return entry;
				}
			}
		}

		return std::nullopt;
	}

	// A folder's removed associations apply to its own entries and the less important folders'
	[[nodiscard]] std::vector<size_t> associatedEntries(const QString& type) const
	{
		std::vector<size_t> entries;
		QSet<QString> removedIds;
		const auto add = [&](const size_t entry) {
			if (!removedIds.contains(_entries[entry].application.id) && std::ranges::find(entries, entry) == entries.end())
				entries.push_back(entry);
		};

		for (const AssociationDir& dir : _dirs)
		{
			for (const Associations& file : dir.files)
			{
				for (const QString& id : file.added.value(type))
				{
					if (const std::optional<size_t> entry = installedEntry(id))
						add(*entry);
				}
			}

			for (const Associations& file : dir.files)
			{
				for (const QString& id : file.removed.value(type))
					removedIds.insert(id);
			}

			for (const size_t entry : dir.entries)
			{
				if (_entries[entry].mimeTypes.contains(type))
					add(entry);
			}
		}

		return entries;
	}

	std::vector<InstalledEntry> _entries;
	QHash<QString, size_t> _entryById;
	std::vector<AssociationDir> _dirs; // Most important first: the config folders, then the applications folders
};

}

std::vector<Application> XdgApplications::applicationsForFileTypes(const std::vector<QStringList>& fileTypes, const Environment& environment)
{
	if (fileTypes.empty())
		return {};

	const Index index{ environment };
	std::vector<size_t> entries = index.applicationsForFileType(fileTypes.front());
	for (const QStringList& typeWithAncestors : fileTypes | std::views::drop(1))
	{
		const std::vector<size_t> fileEntries = index.applicationsForFileType(typeWithAncestors);
		std::erase_if(entries, [&fileEntries](const size_t entry) { return std::ranges::find(fileEntries, entry) == fileEntries.end(); });
	}

	std::vector<Application> applications;
	applications.reserve(entries.size());
	for (const size_t entry : entries)
		applications.push_back(index.application(entry));

	return applications;
}

// The names `type` is known by, in applicationsForFileTypes' order
static QStringList typeNames(const QMimeType& type, const QMimeDatabase& database)
{
	QStringList names{ type.name() };
	names += type.aliases();
	for (const QString& ancestor : type.allAncestors())
	{
		names.push_back(ancestor);
		names += database.mimeTypeForName(ancestor).aliases();
	}

	return names;
}

std::vector<Application> XdgApplications::applicationsToOpen(const QStringList& paths, const Environment& environment)
{
	// Only the first files are read where the name does not give the type: each read takes up to 16 KB, slow on a network volume
	static constexpr qsizetype filesReadForType = 5;

	const QMimeDatabase database;
	std::vector<QStringList> fileTypes; // One entry per distinct type: every file of a type has the same applications
	for (qsizetype i = 0; i < paths.size(); ++i)
	{
		const bool readContents = i < filesReadForType;
		const QMimeType type = database.mimeTypeForFile(paths[i], readContents ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension);
		// Past those, a name that gives no type yields application/octet-stream, which few applications open: the file is left out instead
		if (!readContents && type.isDefault())
			continue;

		QStringList names = typeNames(type, database);
		if (std::ranges::find(fileTypes, names) == fileTypes.end())
			fileTypes.push_back(std::move(names));
	}

	return applicationsForFileTypes(fileTypes, environment);
}

// Whether `word` holds a field code among `codes`
static bool hasFieldCode(const QStringView word, const QStringView codes)
{
	for (qsizetype i = 0; i + 1 < word.size(); ++i)
	{
		if (word[i] == '%' && codes.contains(word[++i]))
			return true;
	}

	return false;
}

// Appends `word` to `commandLine` with its field codes expanded; `files` holds this command line's files.
// A word of field codes alone that expand to nothing is dropped.
static void appendExpandedWord(const QString& word, const QStringList& files, const Application& application, QStringList& commandLine)
{
	if (word == QStringLiteral("%F") || word == QStringLiteral("%U"))
	{
		commandLine += files;
		return;
	}

	if (word == QStringLiteral("%i"))
	{
		if (!application.icon.isEmpty())
			commandLine << QStringLiteral("--icon") << application.icon;
		return;
	}

	QString expanded;
	bool hasCode = false;
	for (qsizetype i = 0; i < word.size(); ++i)
	{
		if (word[i] != '%' || i + 1 == word.size())
		{
			expanded += word[i];
			continue;
		}

		hasCode = true;
		switch (word[++i].unicode())
		{
		case '%':
			expanded += '%';
			break;
		case 'f':
		case 'u':
			if (!files.isEmpty())
				expanded += files.front();
			break;
		case 'c':
			expanded += application.name;
			break;
		case 'k':
			expanded += application.desktopFilePath;
			break;
		default: // The deprecated and unknown codes expand to nothing, and so do %F, %U and %i inside a word
			break;
		}
	}

	if (!hasCode || !expanded.isEmpty())
		commandLine.push_back(std::move(expanded));
}

std::expected<std::vector<QStringList>, QString> XdgApplications::commandLines(const Application& application, const QStringList& files)
{
	const std::optional<QStringList> words = splitShellWords(application.exec);
	if (!words)
		return std::unexpected{ QStringLiteral("Unfinished quote or escape in the command line of %1: %2").arg(application.desktopFilePath, application.exec) };

	std::vector<QStringList> filesByCommandLine;
	if (words->contains(QStringLiteral("%F")) || words->contains(QStringLiteral("%U")))
		filesByCommandLine.push_back(files);
	else if (std::ranges::any_of(*words, [](const QString& word) { return hasFieldCode(word, u"fu"); }) && !files.isEmpty())
	{
		for (const QString& file : files)
			filesByCommandLine.push_back({ file });
	}
	else
		filesByCommandLine.emplace_back();

	std::vector<QStringList> result;
	for (const QStringList& commandLineFiles : filesByCommandLine)
	{
		QStringList& commandLine = result.emplace_back();
		for (const QString& word : *words)
			appendExpandedWord(word, commandLineFiles, application, commandLine);

		if (commandLine.isEmpty())
			return std::unexpected{ QStringLiteral("No program in the command line of %1: %2").arg(application.desktopFilePath, application.exec) };
	}

	return result;
}

#ifndef _WIN32
std::expected<void, QString> XdgApplications::openInApplication(const Application& application, const QStringList& files)
{
	std::expected<std::vector<QStringList>, QString> lines = commandLines(application, files);
	if (!lines)
		return std::unexpected{ std::move(lines.error()) };

	QString workingDir = application.workingDir;
	if (workingDir.isEmpty() && !files.isEmpty())
		workingDir = QFileInfo{ files.front() }.absolutePath();

	for (QStringList& commandLine : *lines)
	{
		std::expected<void, QString> started;
		if (application.terminal)
		{
			// Every word single-quoted: shellQuotedPath would drop a trailing separator, which an argument may need
			for (QString& word : commandLine)
			{
				word.replace('\'', QStringLiteral("'\\''"));
				word = '\'' % word % '\'';
			}
			started = OsShell::runCommandLineInTerminal(commandLine.join(' '), workingDir, false);
		}
		else
		{
			const QString program = commandLine.takeFirst();
			started = OsShell::startDetached(program, std::move(commandLine), workingDir);
		}

		if (!started)
			return started;
	}

	return {};
}
#endif
