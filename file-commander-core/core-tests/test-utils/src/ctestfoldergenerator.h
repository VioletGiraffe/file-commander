#pragma once

class QString;

#include <random>

class CTestFolderGenerator
{
public:

private:
	QString randomString(const size_t length);

private:
	std::mt19937 _rng;
};
