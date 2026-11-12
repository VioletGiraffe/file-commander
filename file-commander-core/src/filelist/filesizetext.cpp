#include "filesizetext.h"

QString compactFileSizeText(const uint64_t size, const FileSizeUnitSpacing spacing)
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

	const uint64_t whole = size / unitSize;
	QString text = QString::number(whole);
	if (whole < 10)
		text += u'.' + QString::number(size % unitSize * 10 / unitSize);
	if (spacing == FileSizeUnitSpacing::Spaced)
		text += u' ';
	return text + QLatin1Char(unitLetters[unit]);
}
