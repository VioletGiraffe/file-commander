#pragma once

#include <QString>
#include <QStringList>

struct QDir_Test
{
	enum Filter {
		Dirs        = 0x001,
		Files       = 0x002,
		Drives      = 0x004,
		NoSymLinks  = 0x008,
		AllEntries  = Dirs | Files | Drives,
		TypeMask    = 0x00f,

		Readable    = 0x010,
		Writable    = 0x020,
		Executable  = 0x040,
		PermissionMask    = 0x070,

		Modified    = 0x080,
		Hidden      = 0x100,
		System      = 0x200,

		AccessMask  = 0x3F0,

		AllDirs       = 0x400,
		CaseSensitive = 0x800,
		NoDot         = 0x2000,
		NoDotDot      = 0x4000,
		NoDotAndDotDot = NoDot | NoDotDot,

		NoFilter = -1
	};
	Q_DECLARE_FLAGS(Filters, Filter)

	enum SortFlag { Name        = 0x00,
					Time        = 0x01,
					Size        = 0x02,
					Unsorted    = 0x03,
					SortByMask  = 0x03,

					DirsFirst   = 0x04,
					Reversed    = 0x08,
					IgnoreCase  = 0x10,
					DirsLast    = 0x20,
					LocaleAware = 0x40,
					Type        = 0x80,
					NoSort = -1
	};
	Q_DECLARE_FLAGS(SortFlags, SortFlag)

	QDir_Test() {}
	QDir_Test(const QString& dirPath) {setPath(dirPath);}

	QString _path;
	void setPath(const QString& path) {_path = path;}

	QString _absolutePath;
	QString absolutePath() const {return _absolutePath;}

	QStringList _entryList;
	QStringList entryList(Filters filters = NoFilter, SortFlags sort = NoSort) const {return _entryList;}
};
