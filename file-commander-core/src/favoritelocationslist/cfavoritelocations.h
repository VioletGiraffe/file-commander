#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

struct CLocationsCollection
{
	explicit CLocationsCollection(QString name, QString path = {}): displayName{std::move(name)}, absolutePath{std::move(path)} {}

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
	void addItem(std::vector<CLocationsCollection>& list, const QString& name, const QString& path = {});

	void save();

private:
	void load();

private:
	const QString _settingsKey;
	std::vector<CLocationsCollection> _items;
};
