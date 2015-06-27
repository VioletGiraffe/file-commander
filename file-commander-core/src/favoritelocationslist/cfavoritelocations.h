#pragma once

#include <QString>

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
