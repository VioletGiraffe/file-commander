#include "progressdialoghelpers.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QObject>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

QString secondsToTimeIntervalString(uint32_t secondsTotal)
{
	const auto hours = secondsTotal / 3600;
	const auto minutes = (secondsTotal - (hours * 3600)) / 60;
	const auto seconds = secondsTotal % 60;

	QString result;
	if (hours > 0)
		result = QString::number(hours) % ' ' % QObject::tr("hours");

	if (minutes > 0)
	{
		if (hours > 0)
			result += ' ';
		result += QString::number(minutes) % ' ' % QObject::tr("minutes");
	}

	if (minutes > 0 || hours > 0)
		result += ' ';

	result += QString::number(seconds) % ' ' % QObject::tr("seconds");

	return result;
}
