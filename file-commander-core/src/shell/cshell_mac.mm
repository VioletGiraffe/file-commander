#include "cshell.h"
#include "assert/advanced_assert.h"

#include <QDebug>

#import <Foundation/Foundation.h>
#import <AppKit/NSWorkspace.h>

#include <stdint.h>
#include <sys/syslimits.h>

NSString* wstring2nsstring(const std::wstring& str)
{
	if (!str.empty())
	{
		assert_and_return_r(str.length() <= PATH_MAX, @"");

		uint16_t utf16string[PATH_MAX + 1];
		for (size_t i = 0, n = str.length(); i < n; ++i)
		{
			assert_r(str[i] <= std::numeric_limits<uint16_t>::max());
			utf16string[i] = static_cast<uint16_t>(str[i]);
		}
		utf16string[str.length()] = 0; // null-terminator

		return [NSString stringWithFormat:@"%S", const_cast<const uint16_t*>(utf16string)];
	}
	else
		return @"";
}

bool OsShell::deleteItems(const std::vector<std::wstring>& items, bool moveToTrash, void* /*parentWindow*/)
{
	assert_and_return_message_r(moveToTrash, "This method can only move files to trash", false);

	NSMutableArray<NSURL *> *files = [[NSMutableArray alloc] init];

	@try {
		for (auto &&path : items)
		{
			NSString *str = wstring2nsstring(path);
			NSURL *url = [[NSURL alloc] initFileURLWithPath:str];
			[files addObject:url];
		}
	}
	@catch(NSException* e) {
		qInfo() << QString::fromNSString(e.description);
		return false;
	}

	[[NSWorkspace sharedWorkspace] recycleURLs:files completionHandler:^(NSDictionary* /*newURLs*/, NSError *error) {
		if (error != nil)
			qInfo() << QString::fromNSString(error.localizedDescription);
	}];

	return true;
}
