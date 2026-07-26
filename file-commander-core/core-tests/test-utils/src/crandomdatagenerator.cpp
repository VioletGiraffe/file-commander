#include "crandomdatagenerator.h"
#include "compiler/compiler_warnings_control.h"

#include <stdint.h>

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

void CRandomDataGenerator::setSeed(uint32_t seed)
{
	_rng = decltype(_rng)(seed);
}

QString CRandomDataGenerator::randomString(const size_t length)
{
	QString resultString;
	resultString.reserve(static_cast<qsizetype>(length));

	for (size_t i = 0; i < length; ++i)
		resultString.append(QChar{ randomNumber<char16_t>(u'A', u'Z') });

	return resultString;
}
