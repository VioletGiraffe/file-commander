#include "cfavoritelocations.h"
#include "settings.h"
#include "settings/csettings.h"
#include "assert/advanced_assert.h"

#include <stack>
#include <functional>

enum Marker {NoMarker, NextLevel, LevelEnded};

static inline void serialize(QByteArray& dest, const CLocationsCollection& source, Marker marker)
{
	QByteArray utfStringData = source.displayName.toUtf8();
	int length = utfStringData.length();
	assert_r(length > 0);
	dest.append((char*)&length, sizeof(length));
	dest.append(utfStringData);

	utfStringData = source.absolutePath.toUtf8();
	length = utfStringData.length();
	dest.append((char*)&length, sizeof(length));
	dest.append(utfStringData);

	if (!source.subLocations.empty())
	{
		int m = NextLevel;
		dest.append((char*)&m, sizeof(m));

		size_t i = 0;
		for(auto subItem = source.subLocations.begin(); subItem != source.subLocations.end(); ++subItem, ++i)
			serialize(dest, *subItem, i == (source.subLocations.size() - 1) ? LevelEnded : NoMarker);
	}
	else
	{
		QString markerName = "NoMarker";
		if (marker == NextLevel)
			markerName = "NextLevel marker";
		else if (marker == LevelEnded)
			markerName = "LevelEnded marker";
		dest.append((char*)&marker, sizeof(marker));
	}
}

CFavoriteLocations::CFavoriteLocations()
{
	load(CSettings().value(KEY_FAVORITES).toByteArray());
}

CFavoriteLocations::~CFavoriteLocations()
{
	save();
}

std::list<CLocationsCollection>& CFavoriteLocations::locations()
{
	return _items;
}

void CFavoriteLocations::load(const QByteArray& data)
{
	int currentPosition = 0;
	_items.clear();
	std::stack<std::reference_wrapper<std::list<CLocationsCollection>>> currentList;
	currentList.push(std::ref(_items));

	while (currentPosition < data.size())
	{
		int length = *(int*)(data.constData()+currentPosition);
		assert_r(length > 0);
		currentPosition += sizeof(length);
		const QString displayName = QString::fromUtf8(data.constData()+currentPosition, length);
		currentPosition += length;

		length = *(int*)(data.constData()+currentPosition);
		currentPosition += sizeof(length);
		QString path;
		if (length > 0)
		{
			path = QString::fromUtf8(data.constData()+currentPosition, length);
			currentPosition += length;
		}

		currentList.top().get().push_back(CLocationsCollection(displayName, path));
		const Marker marker = *reinterpret_cast<const Marker*>(data.constData()+currentPosition);
		currentPosition += sizeof(Marker);

		if (marker == NextLevel)
			currentList.push(currentList.top().get().back().subLocations);
		else if (marker == LevelEnded)
			currentList.pop();
	}
}

void CFavoriteLocations::addItem(std::list<CLocationsCollection>& list, const QString& name, const QString& path)
{
	list.emplace_back(name, path);
	save();
}

void CFavoriteLocations::save()
{
	QByteArray data;
	for (const CLocationsCollection& item : _items)
		serialize(data, item, NoMarker);
	CSettings().setValue(KEY_FAVORITES, data);
}
