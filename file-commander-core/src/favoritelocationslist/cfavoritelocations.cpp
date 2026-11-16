#include "cfavoritelocations.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "utility/memory_cast.hpp"


DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QObject>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <stack>

// Each item is its name and its path, each an int length and UTF-8 bytes, then an int marker:
//   NoMarker - the next item is on the same level
//   NextLevel - the item's subLocations follow
//   LevelEnded + n - 1 - the item is the last of n levels, its own included
constexpr int NoMarker = 0, NextLevel = 1, LevelEnded = 2;

static void appendInt(QByteArray& dest, const int value)
{
	dest.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

// levelsEnded: how many levels end with this item, its own included
static void serialize(QByteArray& dest, const CLocationsCollection& source, const int levelsEnded)
{
	const QByteArray name = source.displayName.toUtf8();
	assert_r(!name.isEmpty());
	appendInt(dest, static_cast<int>(name.size()));
	dest.append(name);

	const QByteArray path = source.absolutePath.toUtf8();
	appendInt(dest, static_cast<int>(path.size()));
	dest.append(path);

	if (source.subLocations.empty())
	{
		appendInt(dest, levelsEnded == 0 ? NoMarker : LevelEnded + levelsEnded - 1);
		return;
	}

	appendInt(dest, NextLevel);
	const size_t count = source.subLocations.size();
	for (size_t i = 0; i < count; ++i)
		serialize(dest, source.subLocations[i], i + 1 == count ? levelsEnded + 1 : 0);
}

CFavoriteLocations::CFavoriteLocations(QString settingsKey) :
	_settingsKey{std::move(settingsKey)}
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

void CFavoriteLocations::load()
{
	const QByteArray data = QSettings().value(_settingsKey).toByteArray();
	if (data.isEmpty())
		return;

	int currentPosition = 0;
	_items.clear();
	std::stack<std::reference_wrapper<std::vector<CLocationsCollection>>> currentList;
	currentList.push(std::ref(_items));

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

		currentList.top().get().emplace_back(displayName, path);

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
}

// skippedIndex: the item being edited, which may keep its own name and path
static std::optional<UserMessage> refusalFor(const std::vector<CLocationsCollection>& list, const std::optional<size_t> skippedIndex, const QString& name,
	const QString& path, const bool isLocation)
{
	const auto refusal = [](const QString& text) {
		return UserMessage{ MessageSeverity::Information, QObject::tr("Similar item already exists"), text };
	};
	const auto anyOther = [&](const auto& matches) {
		for (size_t i = 0; i < list.size(); ++i)
		{
			if (i != skippedIndex && matches(list[i]))
				return true;
		}
		return false;
	};

	if (name.isEmpty())
		return UserMessage{ MessageSeverity::Information, QObject::tr("No name"), QObject::tr("The name cannot be empty.") };
	if (isLocation && path.isEmpty())
		return UserMessage{ MessageSeverity::Information, QObject::tr("No location"), QObject::tr("The location cannot be empty.") };
	if (isLocation && anyOther([&path](const CLocationsCollection& item) { return item.absolutePath == path; }))
		return refusal(QObject::tr("This item already exists here (possibly under a different name)."));
	if (anyOther([&name](const CLocationsCollection& item) { return item.displayName == name; }))
		return refusal(QObject::tr("An item with the same name already exists here (possibly pointing to a different location)."));
	return {};
}

std::optional<UserMessage> CFavoriteLocations::addLocation(std::vector<CLocationsCollection>& list, const QString& name, const QString& path)
{
	if (auto refusal = refusalFor(list, std::nullopt, name, path, true))
		return refusal;

	list.emplace_back(name, path);
	save();
	return {};
}

std::optional<UserMessage> CFavoriteLocations::addCategory(std::vector<CLocationsCollection>& list, const QString& name)
{
	if (auto refusal = refusalFor(list, std::nullopt, name, {}, false))
		return refusal;

	list.emplace_back(name);
	save();
	return {};
}

std::optional<UserMessage> CFavoriteLocations::editItem(std::vector<CLocationsCollection>& list, const size_t index, const QString& name, const QString& path)
{
	assert_and_return_r(index < list.size(), {});
	CLocationsCollection& item = list[index];
	assert_and_return_r(!item.isCategory() || path.isEmpty(), {});
	if (auto refusal = refusalFor(list, index, name, path, !item.isCategory()))
		return refusal;

	item.displayName = name;
	item.absolutePath = path;
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
	for (const CLocationsCollection& item : _items)
		serialize(data, item, 0);
	QSettings().setValue(_settingsKey, data);
}
