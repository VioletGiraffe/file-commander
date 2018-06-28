#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <iostream>

std::ostream& operator<<(std::ostream& stream, const QString& qString)
{
	stream << qString.toStdString();
	return stream;
}