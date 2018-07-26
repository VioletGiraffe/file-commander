#include "cshell.h"
#include "assert/advanced_assert.h"

#include <QDebug>

#include <AppKit/NSWorkspace.h>

NSString* wstring2nsstring(const std::wstring& str)
{
	if (!str.empty())
		return [NSString stringWithCString:str.data() encoding:NSUTF16StringEncoding];
	else
		return @"";
}

bool deleteItems(const std::vector<std::wstring>& items, bool moveToTrash = true, void* /*parentWindow*/ = nullptr);
{
	assert_and_return_message_r(moveToTrash, "This method can only move files to trash", false);

	NSMutableArray<NSURL *> *files = [[NSMutableArray alloc] init];

	for (auto &&path : items)
	{
		NSString *str = wstring2nsstring(path);
		NSURL *url = [[NSURL alloc] initFileURLWithPath:str];
		[files addObject:url];
	}

	[[NSWorkspace sharedWorkspace] recycleURLs:files completionHandler:^(NSDictionary *newURLs, NSError *error) {
		if (error != nil)
		{
			//do something about the error
			qDebug() << QString::fromNSString(error);
			return false;
		}
	}];

	return true;
}
