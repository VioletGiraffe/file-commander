#pragma once

#include "QtCoreIncludes"
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
	std::list<CLocationsCollection>::iterator begin();
	std::list<CLocationsCollection>::iterator end();

private:
	void load(const QByteArray & data);
	void save();

private:
	std::list<CLocationsCollection> _items;
};
