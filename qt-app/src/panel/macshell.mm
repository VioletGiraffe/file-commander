#include "macshell.h"


// Submodule includes
#include "fileoperationsui/usermessagebox.h"


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QProcess>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#import <AppKit/AppKit.h>
#import <Quartz/Quartz.h>

// Autoreleased
static NSArray<NSURL*>* fileUrls(const QStringList& paths)
{
	NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:(NSUInteger)paths.size()];
	for (const QString& path : paths)
		[urls addObject:[NSURL fileURLWithPath:path.toNSString()]];
	return urls;
}

void MacShell::showItemsInFinder(const QStringList& paths)
{
	@autoreleasepool
	{
		[[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:fileUrls(paths)];
	}
}

void MacShell::showFolderInFinder(const QString& folder)
{
	@autoreleasepool
	{
		// Finder by name: another application may be the default for folders
		NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
		if (NSURL* finder = [workspace URLForApplicationWithBundleIdentifier:@"com.apple.finder"])
			[workspace openURLs:fileUrls({ folder }) withApplicationAtURL:finder configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:nil];
	}
}

void MacShell::showInfoInFinder(const QStringList& paths, QWidget* parent)
{
	// The paths reach the script as its arguments: nothing in them needs quoting
	// The aliases are made outside the tell block: inside it, Finder would be asked to resolve POSIX file
	static const QStringList script{
		QStringLiteral("-e"), QStringLiteral("on run itemPaths"),
		QStringLiteral("-e"), QStringLiteral("set itemAliases to {}"),
		QStringLiteral("-e"), QStringLiteral("repeat with itemPath in itemPaths"),
		QStringLiteral("-e"), QStringLiteral("set end of itemAliases to (POSIX file (itemPath as text)) as alias"),
		QStringLiteral("-e"), QStringLiteral("end repeat"),
		QStringLiteral("-e"), QStringLiteral("tell application \"Finder\""),
		QStringLiteral("-e"), QStringLiteral("activate"),
		QStringLiteral("-e"), QStringLiteral("repeat with itemAlias in itemAliases"),
		QStringLiteral("-e"), QStringLiteral("open information window of itemAlias"),
		QStringLiteral("-e"), QStringLiteral("end repeat"),
		QStringLiteral("-e"), QStringLiteral("end tell"),
		QStringLiteral("-e"), QStringLiteral("end run"),
	};

	// Owned by `parent`: the result is dropped once the parent is gone
	auto* osascript = new QProcess{ parent };
	const auto reportFailure = [osascript, parent](const QString& reason) {
		showUserMessage(parent, { MessageSeverity::Error, QCoreApplication::translate("MacShell", "Failed to show the information"), reason });
		osascript->deleteLater();
	};

	QObject::connect(osascript, &QProcess::errorOccurred, parent, [osascript, reportFailure](const QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) // No finished() follows
			reportFailure(osascript->errorString());
	});
	QObject::connect(osascript, &QProcess::finished, parent, [osascript, reportFailure](const int exitCode, const QProcess::ExitStatus status) {
		if (status == QProcess::NormalExit && exitCode == 0)
		{
			osascript->deleteLater();
			return;
		}

		const QString errorOutput = QString::fromLocal8Bit(osascript->readAllStandardError()).trimmed();
		reportFailure(!errorOutput.isEmpty() ? errorOutput : QStringLiteral("osascript failed with exit code %1").arg(exitCode));
	});

	osascript->start(QStringLiteral("/usr/bin/osascript"), script + paths);
}

void MacShell::showShareMenu(const QStringList& paths, const QWidget* widget, const QPoint globalPosition)
{
	const QWidget* window = widget->window();
	// Qt's view of a window is flipped: its coordinates are the widget's, from the top left
	const QPoint position = window->mapFromGlobal(globalPosition);
	@autoreleasepool
	{
		NSSharingServicePicker* picker = [[[NSSharingServicePicker alloc] initWithItems:fileUrls(paths)] autorelease];
		[picker showRelativeToRect:NSMakeRect(position.x(), position.y(), 1, 1) ofView:reinterpret_cast<NSView*>(window->winId()) preferredEdge:NSRectEdgeMaxY];
	}
}

// The panel shows the items of the first object in the key window's chain of responders that accepts control of it
@interface FCQuickLookController : NSResponder <QLPreviewPanelDataSource>
{
	NSArray<NSURL*>* _urls;
}
- (void)setUrls:(NSArray<NSURL*>*)urls;
@end

@implementation FCQuickLookController

- (void)dealloc
{
	[_urls release];
	[super dealloc];
}

- (void)setUrls:(NSArray<NSURL*>*)urls
{
	[urls retain];
	[_urls release];
	_urls = urls;
}

- (BOOL)acceptsPreviewPanelControl:(QLPreviewPanel*)panel
{
	return YES;
}

- (void)beginPreviewPanelControl:(QLPreviewPanel*)panel
{
	panel.dataSource = self;
}

- (void)endPreviewPanelControl:(QLPreviewPanel*)panel
{
	panel.dataSource = nil;
}

- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel*)panel
{
	return (NSInteger)_urls.count;
}

- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel*)panel previewItemAtIndex:(NSInteger)index
{
	return _urls[(NSUInteger)index];
}

@end

void MacShell::showQuickLook(const QStringList& paths, const QWidget* widget)
{
	@autoreleasepool
	{
		// One for the process, never released. It follows the window in the chain: Qt leaves the window's next responder unset.
		static FCQuickLookController* const controller = [[FCQuickLookController alloc] init];
		[controller setUrls:fileUrls(paths)];

		NSWindow* window = reinterpret_cast<NSView*>(widget->window()->winId()).window;
		if (window.nextResponder != controller)
		{
			controller.nextResponder = window.nextResponder;
			window.nextResponder = controller;
		}

		QLPreviewPanel* panel = [QLPreviewPanel sharedPreviewPanel];
		if (panel.visible)
		{
			[panel reloadData];
			panel.currentPreviewItemIndex = 0;
		}
		else
			[panel makeKeyAndOrderFront:nil];
	}
}

std::expected<void, QString> MacShell::ejectVolume(const QString& rootPath)
{
	@autoreleasepool
	{
		NSError* error = nil;
		if ([[NSWorkspace sharedWorkspace] unmountAndEjectDeviceAtURL:[NSURL fileURLWithPath:rootPath.toNSString()] error:&error])
			return {};

		return std::unexpected{ error && error.localizedDescription.length > 0 ? QString::fromNSString(error.localizedDescription) : QStringLiteral("Unknown error") };
	}
}
