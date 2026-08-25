#include "QFileInfo_Test"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

bool QFileInfo_Test::_dummyAbsoluteFilePathGlobal = false;

QString QFileInfo_Test::absoluteFilePath() const
{
	return _dummyAbsoluteFilePath || _dummyAbsoluteFilePathGlobal ? _absoluteFilePath : QFileInfo(_file).absoluteFilePath();
}
