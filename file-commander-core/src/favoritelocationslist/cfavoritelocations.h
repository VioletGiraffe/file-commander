#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/usermessage.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <optional>
#include <utility>
#include <vector>

// A favorite location, a group of items, or a separator between items
struct CLocationsCollection
{
	// Saved as numbers: never renumber
	enum class Kind : uint8_t { Location = 0, Group = 1, Separator = 2 };

	[[nodiscard]] static CLocationsCollection location(QString name, QString path) { return { Kind::Location, std::move(name), std::move(path), {} }; }
	[[nodiscard]] static CLocationsCollection group(QString name) { return { Kind::Group, std::move(name), {}, {} }; }
	[[nodiscard]] static CLocationsCollection separator() { return { Kind::Separator, {}, {}, {} }; }

	[[nodiscard]] bool isGroup() const noexcept { return kind == Kind::Group; }
	[[nodiscard]] bool isSeparator() const noexcept { return kind == Kind::Separator; }

	Kind kind = Kind::Location;
	QString displayName; // Empty for a separator
	QString path; // Only a location has one. As typed: may be relative or hold variables, see resolveUserPath()
	std::vector<CLocationsCollection> subLocations; // Only a group has them
};

// The last component of `path`, or `path` itself when it has none, as a root has not
[[nodiscard]] QString defaultFavoriteName(const QString& path);

class CFavoriteLocations
{
public:
	// Loads from settingsKey, or from legacySettingsKey when settingsKey is absent. Saves to settingsKey only.
	CFavoriteLocations(QString settingsKey, QString legacySettingsKey);
	~CFavoriteLocations();

	CFavoriteLocations(const CFavoriteLocations&) = delete;
	CFavoriteLocations& operator=(const CFavoriteLocations&) = delete;

	std::vector<CLocationsCollection>& locations();
	// Replaces every item and saves
	void setLocations(std::vector<CLocationsCollection> locations);

	// Each saves the change, or returns why it refused: a location without a path, or a group without a name.
	// A location without a name gets defaultFavoriteName(path).
	[[nodiscard]] std::optional<UserMessage> addLocation(std::vector<CLocationsCollection>& list, const QString& name, const QString& path);
	[[nodiscard]] std::optional<UserMessage> addGroup(std::vector<CLocationsCollection>& list, const QString& name);
	// Never called on a separator. A group's path stays empty.
	[[nodiscard]] std::optional<UserMessage> editItem(std::vector<CLocationsCollection>& list, size_t index, const QString& name, const QString& path);
	void removeItem(std::vector<CLocationsCollection>& list, size_t index);

	void save();

private:
	void load();

private:
	const QString _settingsKey;
	const QString _legacySettingsKey;
	std::vector<CLocationsCollection> _items;
};
