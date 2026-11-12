#include "filetimetext.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
RESTORE_COMPILER_WARNINGS

QString fileTimeText(const time_t time, const FileTimeTextLength length)
{
	const QDateTime dateTime = QDateTime::fromSecsSinceEpoch(time);
	// '' is a literal apostrophe in a Qt format. QDateTime::toString(format) uses the C locale.
	if (length == FileTimeTextLength::Long)
		return dateTime.toString(QStringLiteral("dd MMM ''yy hh:mm:ss"));

	return dateTime.toString(dateTime.date().year() == QDate::currentDate().year() ? QStringLiteral("dd MMM hh:mm") : QStringLiteral("dd MMM ''yy"));
}
