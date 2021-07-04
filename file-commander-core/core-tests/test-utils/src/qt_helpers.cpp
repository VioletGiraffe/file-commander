#include "qt_helpers.hpp"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

std::ostream& operator<<(std::ostream& stream, const QString& qString)
{
	stream << qString.toStdString();
	return stream;
}

QString qStringFromWstring(const std::wstring& ws)
{
	return QString::fromWCharArray(ws.data(), (int)ws.size());
}
