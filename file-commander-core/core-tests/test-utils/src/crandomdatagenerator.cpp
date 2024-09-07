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
	resultString.reserve((int)length);

	std::uniform_int_distribution<int16_t> distribution('A', 'Z');
	for (size_t i = 0; i < length; ++i)
	{
		const char ch = static_cast<char>(distribution(_rng));
		resultString.append(QChar(ch));
	}

	return resultString;
}
