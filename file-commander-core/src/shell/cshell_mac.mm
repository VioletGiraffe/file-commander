#include "cshell.h"


#import <Foundation/Foundation.h>
#import <AppKit/NSWorkspace.h>

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
