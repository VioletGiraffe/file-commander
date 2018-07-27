#include "cshell.h"
#include "assert/advanced_assert.h"

#include <QDebug>

#import <Foundation/Foundation.h>
#import <AppKit/NSWorkspace.h>

NSString* wstring2nsstring(const std::wstring& str)
{
	if (!str.empty())
		return [NSString stringWithCString:(const char*)str.data() encoding:NSUTF16StringEncoding];
	else
		return @"";
}

bool OsShell::deleteItems(const std::vector<std::wstring>& items, bool moveToTrash, void* /*parentWindow*/)
{
	assert_and_return_message_r(moveToTrash, "This method can only move files to trash", false);

	NSMutableArray<NSURL *> *files = [[NSMutableArray alloc] init];

	for (auto &&path : items)
	{
		NSString *str = wstring2nsstring(path);
		NSURL *url = [[NSURL alloc] initFileURLWithPath:str];
		[files addObject:url];
	}

	[[NSWorkspace sharedWorkspace] recycleURLs:files completionHandler:^(NSDictionary* /*newURLs*/, NSError *error) {
		if (error != nil)
			qDebug() << QString::fromNSString(error.localizedDescription);
	}];

	return true;
}
