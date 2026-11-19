#include "freedesktopshell.h"


// Submodule includes
#include "fileoperationsui/usermessagebox.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QUrl>
#include <QVariantMap>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <limits>

static constexpr char fileManagerService[] = "org.freedesktop.FileManager1";

bool FreedesktopShell::fileManagerAvailable()
{
	const QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
	if (!bus)
		return false;

	const QString service = QString::fromLatin1(fileManagerService);
	return bus->isServiceRegistered(service).value() || bus->activatableServiceNames().value().contains(service);
}

// Calls `method` with the paths' URIs; a failure is shown with `failureTitle`
static void callFileManager(const char* method, const QStringList& paths, QWidget* parent, const QString& failureTitle)
{
	QStringList uris;
	for (const QString& path : paths)
		uris.push_back(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));

	QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(fileManagerService), QStringLiteral("/org/freedesktop/FileManager1"),
		QString::fromLatin1(fileManagerService), QString::fromLatin1(method));
	call.setArguments({ uris, QString{} }); // The startup notification ID: none

	// Owned by `parent`: the reply is dropped once the parent is gone
	auto* watcher = new QDBusPendingCallWatcher{ QDBusConnection::sessionBus().asyncCall(call), parent };
	QObject::connect(watcher, &QDBusPendingCallWatcher::finished, parent, [watcher, parent, failureTitle] {
		if (watcher->isError())
			showUserMessage(parent, { MessageSeverity::Error, failureTitle, watcher->error().message() });
		watcher->deleteLater();
	});
}

void FreedesktopShell::showFoldersInFileManager(const QStringList& folders, QWidget* parent)
{
	callFileManager("ShowFolders", folders, parent, QCoreApplication::translate("FreedesktopShell", "Failed to show the folder in the file manager"));
}

void FreedesktopShell::showItemsInFileManager(const QStringList& paths, QWidget* parent)
{
	callFileManager("ShowItems", paths, parent, QCoreApplication::translate("FreedesktopShell", "Failed to show the items in the file manager"));
}

void FreedesktopShell::showPropertiesInFileManager(const QStringList& paths, QWidget* parent)
{
	callFileManager("ShowItemProperties", paths, parent, QCoreApplication::translate("FreedesktopShell", "Failed to show the properties"));
}

namespace {

namespace Udisks {
	constexpr char service[] = "org.freedesktop.UDisks2";
	constexpr char manager[] = "/org/freedesktop/UDisks2/Manager";
	constexpr char managerInterface[] = "org.freedesktop.UDisks2.Manager";
	constexpr char blockInterface[] = "org.freedesktop.UDisks2.Block";
	constexpr char filesystemInterface[] = "org.freedesktop.UDisks2.Filesystem";
	constexpr char encryptedInterface[] = "org.freedesktop.UDisks2.Encrypted";
	constexpr char partitionInterface[] = "org.freedesktop.UDisks2.Partition";
	constexpr char loopInterface[] = "org.freedesktop.UDisks2.Loop";
	constexpr char driveInterface[] = "org.freedesktop.UDisks2.Drive";
	constexpr char noObject[] = "/";
}

}

// The reply's arguments, or the error's text
static std::expected<QVariantList, QString> callUdisks(const QString& objectPath, const char* interface, const char* method, const QVariantList& arguments)
{
	QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1(Udisks::service), objectPath, QString::fromLatin1(interface), QString::fromLatin1(method));
	call.setArguments(arguments);
	// No timeout: unmounting flushes the volume, and the user may be asked for authorization meanwhile
	const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, std::numeric_limits<int>::max());
	if (reply.type() == QDBusMessage::ErrorMessage)
		return std::unexpected{ !reply.errorMessage().isEmpty() ? reply.errorMessage() : reply.errorName() };

	return reply.arguments();
}

// Every UDisks method this file calls takes an options dictionary last; none is set
static std::expected<void, QString> callUdisksMethod(const QString& objectPath, const char* interface, const char* method)
{
	const auto reply = callUdisks(objectPath, interface, method, { QVariantMap{} });
	if (!reply)
		return std::unexpected{ reply.error() };
	return {};
}

// Fails when the object has no such interface
static std::expected<QVariant, QString> udisksProperty(const QString& objectPath, const char* interface, const char* property)
{
	const auto reply = callUdisks(objectPath, "org.freedesktop.DBus.Properties", "Get", { QString::fromLatin1(interface), QString::fromLatin1(property) });
	if (!reply)
		return std::unexpected{ reply.error() };

	return qvariant_cast<QDBusVariant>(reply->value(0)).variant();
}

// Udisks::noObject when the property names none
static std::expected<QString, QString> udisksObjectProperty(const QString& objectPath, const char* interface, const char* property)
{
	const auto value = udisksProperty(objectPath, interface, property);
	if (!value)
		return std::unexpected{ value.error() };

	return qvariant_cast<QDBusObjectPath>(*value).path();
}

static bool udisksBoolProperty(const QString& objectPath, const char* interface, const char* property)
{
	const auto value = udisksProperty(objectPath, interface, property);
	return value && value->toBool();
}

// The loop device `block` is or is a partition of; empty when there is none
static QString loopDevice(const QString& block)
{
	if (udisksProperty(block, Udisks::loopInterface, "BackingFile"))
		return block;

	const auto table = udisksObjectProperty(block, Udisks::partitionInterface, "Table");
	return table && udisksProperty(*table, Udisks::loopInterface, "BackingFile") ? *table : QString{};
}

std::expected<void, QString> FreedesktopShell::ejectBlockDevice(const QString& devicePath)
{
	const auto resolved = callUdisks(QString::fromLatin1(Udisks::manager), Udisks::managerInterface, "ResolveDevice",
		{ QVariantMap{ { QStringLiteral("path"), devicePath } }, QVariantMap{} });
	if (!resolved)
		return std::unexpected{ resolved.error() };

	const QList<QDBusObjectPath> blocks = qdbus_cast<QList<QDBusObjectPath>>(resolved->value(0));
	if (blocks.isEmpty())
		return std::unexpected{ QCoreApplication::translate("FreedesktopShell", "UDisks does not manage %1").arg(devicePath) };

	const QString block = blocks.front().path();
	if (const auto unmounted = callUdisksMethod(block, Udisks::filesystemInterface, "Unmount"); !unmounted)
		return unmounted;

	// A LUKS volume's own block has no drive: the drive is that of the device it lies on
	QString driveBlock = block;
	const auto backingDevice = udisksObjectProperty(block, Udisks::blockInterface, "CryptoBackingDevice");
	if (!backingDevice)
		return std::unexpected{ backingDevice.error() };

	if (*backingDevice != QLatin1StringView{ Udisks::noObject })
	{
		if (const auto locked = callUdisksMethod(*backingDevice, Udisks::encryptedInterface, "Lock"); !locked)
			return locked;
		driveBlock = *backingDevice;
	}

	const auto drive = udisksObjectProperty(driveBlock, Udisks::blockInterface, "Drive");
	if (!drive)
		return std::unexpected{ drive.error() };

	// A loop device has no drive: detaching it releases the image file
	if (*drive == QLatin1StringView{ Udisks::noObject })
	{
		const QString loop = loopDevice(driveBlock);
		return loop.isEmpty() ? std::expected<void, QString>{} : callUdisksMethod(loop, Udisks::loopInterface, "Delete");
	}

	// Powering off an optical drive would leave its disc in the tray
	if (udisksBoolProperty(*drive, Udisks::driveInterface, "Optical"))
		return callUdisksMethod(*drive, Udisks::driveInterface, "Eject");
	if (udisksBoolProperty(*drive, Udisks::driveInterface, "CanPowerOff"))
		return callUdisksMethod(*drive, Udisks::driveInterface, "PowerOff");
	if (udisksBoolProperty(*drive, Udisks::driveInterface, "Ejectable"))
		return callUdisksMethod(*drive, Udisks::driveInterface, "Eject");

	return {};
}
