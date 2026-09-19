#include "userprograms.h"

#include "filesystemhelperfunctions.h"
#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QSettings>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <iterator>

namespace {

struct PlaceholderToken
{
	Placeholder placeholder;
	QStringView token;
};

// No token is a prefix of another, so the first match is the only one
constexpr PlaceholderToken placeholderTokens[]{
	{ Placeholder::File,      u"{file}" },
	{ Placeholder::Name,      u"{name}" },
	{ Placeholder::Selection, u"{sel}" },
	{ Placeholder::Dir,       u"{dir}" },
	{ Placeholder::Other,     u"{other}" },
	{ Placeholder::OtherFile, u"{otherfile}" },
};

struct WorkingDirSettingValue
{
	UserProgram::WorkingDir workingDir;
	QStringView value;
};

constexpr WorkingDirSettingValue workingDirSettingValues[]{
	{ UserProgram::WorkingDir::CurrentPanel, u"current" },
	{ UserProgram::WorkingDir::OtherPanel,   u"other" },
	{ UserProgram::WorkingDir::Custom,       u"custom" },
};

QString quotedNativePath(const QString& path)
{
	return shellQuotedPath(toNativeSeparators(path));
}

std::expected<QString, PlaceholderError> placeholderValue(const Placeholder placeholder, const PlaceholderValues& values)
{
	switch (placeholder)
	{
	case Placeholder::File:
		if (values.currentItem.isEmpty())
			return std::unexpected{ PlaceholderError::NoCurrentItem };
		return quotedNativePath(values.currentItem);
	case Placeholder::Name:
		if (values.currentItem.isEmpty())
			return std::unexpected{ PlaceholderError::NoCurrentItem };
		return shellQuotedPath(QFileInfo{ withoutTrailingSeparator(values.currentItem) }.fileName());
	case Placeholder::Selection:
	{
		if (values.selection.empty())
			return std::unexpected{ PlaceholderError::NoCurrentItem };

		QString paths;
		for (const QString& path : values.selection)
		{
			if (!paths.isEmpty())
				paths += ' ';
			paths += quotedNativePath(path);
		}
		return paths;
	}
	case Placeholder::Dir:
		return quotedNativePath(values.currentDir);
	case Placeholder::Other:
		return quotedNativePath(values.otherDir);
	case Placeholder::OtherFile:
		if (values.otherItem.isEmpty())
			return std::unexpected{ PlaceholderError::NoOtherItem };
		return quotedNativePath(values.otherItem);
	}

	return {};
}

} // namespace

std::vector<UserProgram> loadUserPrograms()
{
	QSettings settings;
	const int count = settings.beginReadArray(KEY_USER_PROGRAMS);

	std::vector<UserProgram> programs;
	programs.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		settings.setArrayIndex(i);

		UserProgram& program = programs.emplace_back();
		program.name = settings.value(QStringLiteral("name")).toString();
		program.commandLine = settings.value(QStringLiteral("commandLine")).toString();
		program.customWorkingDir = settings.value(QStringLiteral("customWorkingDir")).toString();
		program.editBeforeRunning = settings.value(QStringLiteral("editBeforeRunning")).toBool();

		const QString workingDir = settings.value(QStringLiteral("workingDir")).toString();
		const auto* match = std::find_if(std::begin(workingDirSettingValues), std::end(workingDirSettingValues), [&](const WorkingDirSettingValue& entry) { return entry.value == workingDir; });
		if (match != std::end(workingDirSettingValues))
			program.workingDir = match->workingDir;
	}

	settings.endArray();
	return programs;
}

void saveUserPrograms(const std::vector<UserProgram>& programs)
{
	QSettings settings;
	settings.remove(KEY_USER_PROGRAMS); // beginWriteArray keeps the entries past the new size
	settings.beginWriteArray(KEY_USER_PROGRAMS, static_cast<int>(programs.size()));
	for (int i = 0; i < static_cast<int>(programs.size()); ++i)
	{
		const UserProgram& program = programs[static_cast<size_t>(i)];
		settings.setArrayIndex(i);
		settings.setValue(QStringLiteral("name"), program.name);
		settings.setValue(QStringLiteral("commandLine"), program.commandLine);
		settings.setValue(QStringLiteral("customWorkingDir"), program.customWorkingDir);
		settings.setValue(QStringLiteral("editBeforeRunning"), program.editBeforeRunning);

		const auto* match = std::find_if(std::begin(workingDirSettingValues), std::end(workingDirSettingValues), [&](const WorkingDirSettingValue& entry) { return entry.workingDir == program.workingDir; });
		assert_r(match != std::end(workingDirSettingValues));
		settings.setValue(QStringLiteral("workingDir"), match->value.toString());
	}

	settings.endArray();
}

QString placeholderToken(const Placeholder placeholder)
{
	const auto* match = std::find_if(std::begin(placeholderTokens), std::end(placeholderTokens), [&](const PlaceholderToken& entry) { return entry.placeholder == placeholder; });
	assert_and_return_r(match != std::end(placeholderTokens), {});
	return match->token.toString();
}

QString workingDirFor(const UserProgram& program, const PlaceholderValues& values)
{
	switch (program.workingDir)
	{
	case UserProgram::WorkingDir::CurrentPanel:
		return toNativeSeparators(values.currentDir);
	case UserProgram::WorkingDir::OtherPanel:
		return toNativeSeparators(values.otherDir);
	case UserProgram::WorkingDir::Custom:
		return toNativeSeparators(program.customWorkingDir);
	}

	return {};
}

std::expected<QString, PlaceholderError> expandPlaceholders(const QString& commandLine, const PlaceholderValues& values)
{
	QString expanded;
	expanded.reserve(commandLine.size());

	// A single pass: a value containing a token is not expanded again
	for (qsizetype i = 0; i < commandLine.size();)
	{
		const QStringView rest = QStringView{ commandLine }.sliced(i);
		const auto* match = std::find_if(std::begin(placeholderTokens), std::end(placeholderTokens), [&](const PlaceholderToken& entry) { return rest.startsWith(entry.token); });
		if (match == std::end(placeholderTokens))
		{
			expanded += commandLine[i++];
			continue;
		}

		auto value = placeholderValue(match->placeholder, values);
		if (!value)
			return std::unexpected{ value.error() };

		expanded += *value;
		i += match->token.size();
	}

	return expanded;
}
