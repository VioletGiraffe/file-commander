#include "cfavoritelocations.h"

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "utility/memory_cast.hpp"


DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QIODevice>
#include <QObject>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <stack>

using Kind = CLocationsCollection::Kind;

// The data is a quint32 count of the top-level items, then the items. Each item is its Kind as a quint8, then:
//   Location - its name and path
//   Category - its name, a quint32 count of its items, then the items
//   Separator - nothing
// A different layout goes under a new settings key: earlier versions keep reading this one.
static void writeItem(QDataStream& stream, const CLocationsCollection& item)
{
	stream << static_cast<quint8>(item.kind);
	switch (item.kind)
	{
	case Kind::Location:
		stream << item.displayName << item.path;
		break;
	case Kind::Category:
		stream << item.displayName << static_cast<quint32>(item.subLocations.size());
		for (const CLocationsCollection& subItem : item.subLocations)
			writeItem(stream, subItem);
		break;
	case Kind::Separator:
		break;
	}
}

// False at the first item that fails to read: settings data may be truncated or corrupted. The items read before it are kept.
// depth: of the categories holding `items`
static bool readItems(QDataStream& stream, const quint32 count, std::vector<CLocationsCollection>& items, const int depth)
{
	// Deeper nesting is corrupted data, and each level takes a stack frame
	static constexpr int MaxDepth = 100;
	if (count > 0 && depth > MaxDepth)
		return false;

	for (quint32 i = 0; i < count; ++i)
	{
		quint8 kind = 0;
		stream >> kind;
		CLocationsCollection item;
		quint32 subItemCount = 0;
		switch (static_cast<Kind>(kind))
		{
		case Kind::Location:
			item = CLocationsCollection::location({}, {});
			stream >> item.displayName >> item.path;
			break;
		case Kind::Category:
			item = CLocationsCollection::category({});
			stream >> item.displayName >> subItemCount;
			break;
		case Kind::Separator:
			item = CLocationsCollection::separator();
			break;
		default:
			return false;
		}

		if (stream.status() != QDataStream::Ok)
			return false;

		items.push_back(std::move(item));
		if (!readItems(stream, subItemCount, items.back().subLocations, depth + 1))
			return false;
	}
	return true;
}

// The format of earlier versions: each item is its name and its path, each an int length and UTF-8 bytes, then an int marker:
//   NoMarker - the next item is on the same level
//   NextLevel - the item's subLocations follow
//   LevelEnded + n - 1 - the item is the last of n levels, its own included
// An item without a path is a category.
constexpr int NoMarker = 0, NextLevel = 1, LevelEnded = 2;

static std::vector<CLocationsCollection> legacyFormatItems(const QByteArray& data)
{
	std::vector<CLocationsCollection> items;
	int currentPosition = 0;
	std::stack<std::reference_wrapper<std::vector<CLocationsCollection>>> currentList;
	currentList.push(std::ref(items));

	// Guards against corrupted or truncated settings data, which would otherwise cause out-of-bounds reads below.
	const auto canRead = [&data](int pos, int n) {
		return n >= 0 && pos >= 0 && n <= data.size() - pos;
	};

	while (currentPosition < data.size())
	{
		if (!canRead(currentPosition, static_cast<int>(sizeof(int))))
			break;

		int length = memory_cast<int>(data.constData()+currentPosition);
		assert_r(length > 0);
		currentPosition += sizeof(length);
		if (!canRead(currentPosition, length))
			break;
		const QString displayName = QString::fromUtf8(data.constData()+currentPosition, length);
		currentPosition += length;

		if (!canRead(currentPosition, static_cast<int>(sizeof(int))))
			break;
		length = memory_cast<int>(data.constData()+currentPosition);
		currentPosition += sizeof(length);
		QString path;
		if (length > 0)
		{
			if (!canRead(currentPosition, length))
				break;
			path = QString::fromUtf8(data.constData()+currentPosition, length);
			currentPosition += length;
		}

		currentList.top().get().push_back(path.isEmpty() ? CLocationsCollection::category(displayName) : CLocationsCollection::location(displayName, path));

		if (!canRead(currentPosition, static_cast<int>(sizeof(int))))
			break;
		const int marker = memory_cast<int>(data.constData()+currentPosition);
		currentPosition += sizeof(int);

		if (marker == NextLevel)
			currentList.emplace(currentList.top().get().back().subLocations);
		// The root list is never popped: corrupted data could otherwise end more levels than there are
		for (int levelsEnded = marker >= LevelEnded ? marker - LevelEnded + 1 : 0; levelsEnded > 0 && currentList.size() > 1; --levelsEnded)
			currentList.pop();
	}
	return items;
}

QString defaultFavoriteName(const QString& path)
{
	const QString lastComponent = QDir::cleanPath(toPosixSeparators(path)).section(u'/', -1);
	return lastComponent.isEmpty() ? path : lastComponent;
}

CFavoriteLocations::CFavoriteLocations(QString settingsKey, QString legacySettingsKey) :
	_settingsKey{std::move(settingsKey)},
	_legacySettingsKey{std::move(legacySettingsKey)}
{
	load();
}

CFavoriteLocations::~CFavoriteLocations()
{
	save();
}

std::vector<CLocationsCollection>& CFavoriteLocations::locations()
{
	return _items;
}

void CFavoriteLocations::setLocations(std::vector<CLocationsCollection> locations)
{
	_items = std::move(locations);
	save();
}

void CFavoriteLocations::load()
{
	QSettings settings;
	if (!settings.contains(_settingsKey))
	{
		_items = legacyFormatItems(settings.value(_legacySettingsKey).toByteArray());
		return;
	}

	QByteArray data = settings.value(_settingsKey).toByteArray();
	QDataStream stream{ &data, QIODevice::ReadOnly };
	stream.setVersion(QDataStream::Qt_6_0);
	quint32 count = 0;
	stream >> count;
	_items.clear();
	if (stream.status() == QDataStream::Ok)
		readItems(stream, count, _items, 0);
}

static std::optional<UserMessage> refusalFor(const Kind kind, const QString& name, const QString& path)
{
	if (kind == Kind::Category && name.isEmpty())
		return UserMessage{ MessageSeverity::Information, QObject::tr("No name"), QObject::tr("The name cannot be empty.") };
	if (kind == Kind::Location && path.isEmpty())
		return UserMessage{ MessageSeverity::Information, QObject::tr("No location"), QObject::tr("The location cannot be empty.") };
	return {};
}

std::optional<UserMessage> CFavoriteLocations::addLocation(std::vector<CLocationsCollection>& list, const QString& name, const QString& path)
{
	if (auto refusal = refusalFor(Kind::Location, name, path))
		return refusal;

	list.push_back(CLocationsCollection::location(name.isEmpty() ? defaultFavoriteName(path) : name, path));
	save();
	return {};
}

std::optional<UserMessage> CFavoriteLocations::addCategory(std::vector<CLocationsCollection>& list, const QString& name)
{
	if (auto refusal = refusalFor(Kind::Category, name, {}))
		return refusal;

	list.push_back(CLocationsCollection::category(name));
	save();
	return {};
}

std::optional<UserMessage> CFavoriteLocations::editItem(std::vector<CLocationsCollection>& list, const size_t index, const QString& name, const QString& path)
{
	assert_and_return_r(index < list.size(), {});
	CLocationsCollection& item = list[index];
	assert_and_return_r(!item.isSeparator(), {});
	assert_and_return_r(!item.isCategory() || path.isEmpty(), {});
	if (auto refusal = refusalFor(item.kind, name, path))
		return refusal;

	item.displayName = item.isCategory() || !name.isEmpty() ? name : defaultFavoriteName(path);
	item.path = path;
	save();
	return {};
}

void CFavoriteLocations::removeItem(std::vector<CLocationsCollection>& list, const size_t index)
{
	assert_and_return_r(index < list.size(), );
	list.erase(list.begin() + (ptrdiff_t)index);
	save();
}

void CFavoriteLocations::save()
{
	QByteArray data;
	QDataStream stream{ &data, QIODevice::WriteOnly };
	stream.setVersion(QDataStream::Qt_6_0);
	stream << static_cast<quint32>(_items.size());
	for (const CLocationsCollection& item : _items)
		writeItem(stream, item);
	QSettings().setValue(_settingsKey, data);
}
