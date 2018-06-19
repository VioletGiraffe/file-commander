#include "QFileInfo_Test"

#include <QFileInfo>

bool QFileInfo_Test::_dummyAbsoluteFilePathGlobal = false;

QString QFileInfo_Test::absoluteFilePath() const
{
	return _dummyAbsoluteFilePath || _dummyAbsoluteFilePathGlobal ? _absoluteFilePath : QFileInfo(_file).absoluteFilePath();
}
