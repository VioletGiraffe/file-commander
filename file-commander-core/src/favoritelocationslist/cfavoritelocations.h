#pragma once

#include "utils/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <list>

struct CLocationsCollection
{
	explicit CLocationsCollection(const QString& name, const QString& path = QString()): displayName(name), absolutePath(path) {}

	QString displayName;
	QString absolutePath;

	std::list<CLocationsCollection> subLocations;
};

class CFavoriteLocations
{
public:
	CFavoriteLocations();
	~CFavoriteLocations();

	std::list<CLocationsCollection>& locations();
	void addItem(std::list<CLocationsCollection>& list, const QString& name, const QString& path = QString());

	void save();

private:
	void load(const QByteArray & data);

private:
	std::list<CLocationsCollection> _items;
};
