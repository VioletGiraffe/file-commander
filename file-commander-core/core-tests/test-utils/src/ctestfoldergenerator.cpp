#include "ctestfoldergenerator.h"

#include <QString>

#include <vector>

QString CTestFolderGenerator::randomString(const size_t length)
{
	std::vector<QChar> chars;
	chars.reserve(length);
	std::uniform_int_distribution<short> distribution('a', 'z');
	for (size_t i = 0; i < length; ++i)
		chars.emplace_back(distribution(_rng));

	return QString(chars.data(), (int)chars.size());
}
