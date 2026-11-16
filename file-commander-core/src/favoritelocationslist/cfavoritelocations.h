#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/usermessage.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <vector>

struct CLocationsCollection
{
	explicit CLocationsCollection(QString name, QString path = {}): displayName{std::move(name)}, absolutePath{std::move(path)} {}

	// A category has no path, and only a category has subLocations
	[[nodiscard]] bool isCategory() const noexcept { return absolutePath.isEmpty(); }

	QString displayName;
	QString absolutePath;

	std::vector<CLocationsCollection> subLocations;
};

class CFavoriteLocations
{
public:
	explicit CFavoriteLocations(QString settingsKey);
	~CFavoriteLocations();

	CFavoriteLocations(const CFavoriteLocations&) = delete;
	CFavoriteLocations& operator=(const CFavoriteLocations&) = delete;

	std::vector<CLocationsCollection>& locations();

	// Each saves the change, or returns why it refused: an empty name, a name another item of `list` has,
	// or for a location, an empty path or a path another location of `list` has.
	[[nodiscard]] std::optional<UserMessage> addLocation(std::vector<CLocationsCollection>& list, const QString& name, const QString& path);
	[[nodiscard]] std::optional<UserMessage> addCategory(std::vector<CLocationsCollection>& list, const QString& name);
	// A category's path stays empty
	[[nodiscard]] std::optional<UserMessage> editItem(std::vector<CLocationsCollection>& list, size_t index, const QString& name, const QString& path);
	void removeItem(std::vector<CLocationsCollection>& list, size_t index);

	void save();

private:
	void load();

private:
	const QString _settingsKey;
	std::vector<CLocationsCollection> _items;
};
