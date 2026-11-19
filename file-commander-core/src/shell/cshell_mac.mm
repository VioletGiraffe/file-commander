#include "cshell.h"


#import <Foundation/Foundation.h>
#import <AppKit/NSWorkspace.h>

#include <utility>

std::vector<OsShell::TrashFailure> OsShell::moveItemsToTrashThroughNSWorkspace(const QStringList& paths)
{
	std::vector<TrashFailure> failures;
	@autoreleasepool
	{
		NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:(NSUInteger)paths.size()];
		for (const QString& path : paths)
			[urls addObject:[NSURL fileURLWithPath:path.toNSString()]];

		// Retained: the completion handler's arguments are not guaranteed to outlive it
		__block NSDictionary<NSURL*, NSURL*>* trashedUrls = nil;
		__block NSError* trashError = nil;
		dispatch_semaphore_t done = dispatch_semaphore_create(0);
		[[NSWorkspace sharedWorkspace] recycleURLs:urls completionHandler:^(NSDictionary<NSURL*, NSURL*>* newURLs, NSError* error) {
			trashedUrls = [newURLs retain];
			trashError = [error retain];
			dispatch_semaphore_signal(done);
		}];
		dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
		dispatch_release(done);

		// One error describes the whole batch
		const QString reason = trashError ? QString::fromNSString(trashError.localizedDescription) : QStringLiteral("Unknown error");
		for (NSUInteger i = 0; i < urls.count; ++i)
		{
			if (trashedUrls[urls[i]] == nil)
				failures.push_back({ .path = paths[(qsizetype)i], .reason = reason });
		}

		[trashedUrls release];
		[trashError release];
	}

	return failures;
}

static QString standardizedPath(NSURL* url)
{
	return QString::fromNSString(url.URLByStandardizingPath.path);
}

std::vector<OsShell::ApplicationBundle> OsShell::applicationsToOpen(const QStringList& paths)
{
	std::vector<ApplicationBundle> applications;
	@autoreleasepool
	{
		NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
		QStringList applicationPaths; // The first path's, in preference order
		for (qsizetype i = 0; i < paths.size(); ++i)
		{
			NSURL* url = [NSURL fileURLWithPath:paths[i].toNSString()];
			QStringList pathApplications;
			if (NSURL* defaultApplication = i == 0 ? [workspace URLForApplicationToOpenURL:url] : nil)
				pathApplications.push_back(standardizedPath(defaultApplication));
			for (NSURL* application in [workspace URLsForApplicationsToOpenURL:url])
			{
				if (const QString applicationPath = standardizedPath(application); !pathApplications.contains(applicationPath))
					pathApplications.push_back(applicationPath);
			}

			if (i == 0)
				applicationPaths = std::move(pathApplications);
			else
				applicationPaths.removeIf([&pathApplications](const QString& applicationPath) { return !pathApplications.contains(applicationPath); });
		}

		for (const QString& applicationPath : applicationPaths)
		{
			NSString* name = nil;
			[[NSURL fileURLWithPath:applicationPath.toNSString()] getResourceValue:&name forKey:NSURLLocalizedNameKey error:nil];
			QString displayName = name ? QString::fromNSString(name) : applicationPath.section('/', -1);
			if (displayName.endsWith(QStringLiteral(".app")))
				displayName.chop(4);

			applications.push_back({ .name = std::move(displayName), .path = applicationPath });
		}
	}

	return applications;
}

void OsShell::openInApplication(const QStringList& paths, const QString& applicationPath, std::function<void(const QString& reason)> onFailure)
{
	@autoreleasepool
	{
		NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:(NSUInteger)paths.size()];
		for (const QString& path : paths)
			[urls addObject:[NSURL fileURLWithPath:path.toNSString()]];

		// The handler runs on a background queue
		[[NSWorkspace sharedWorkspace] openURLs:urls withApplicationAtURL:[NSURL fileURLWithPath:applicationPath.toNSString()]
			configuration:[NSWorkspaceOpenConfiguration configuration] completionHandler:^(NSRunningApplication*, NSError* error) {
				if (!error)
					return;

				const QString reason = error.localizedDescription.length > 0 ? QString::fromNSString(error.localizedDescription) : QStringLiteral("Unknown error");
				dispatch_async(dispatch_get_main_queue(), ^{
					onFailure(reason);
				});
			}];
	}
}
