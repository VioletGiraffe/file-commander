#include "filesizetext.h"

QString compactFileSizeText(const uint64_t size)
{
	if (size < 1024)
		return QString::number(size);

	static constexpr char unitLetters[]{ 'K', 'M', 'G', 'T', 'P', 'E' }; // UINT64_MAX is 15E
	uint64_t unitSize = 1024;
	int unit = 0;
	while (size / unitSize >= 1024)
	{
		unitSize *= 1024;
		++unit;
	}

	const QChar unitLetter = QLatin1Char(unitLetters[unit]);
	const uint64_t whole = size / unitSize;
	if (whole >= 10)
		return QString::number(whole) + unitLetter;
	return QString::number(whole) + u'.' + QString::number(size % unitSize * 10 / unitSize) + unitLetter;
}
