#pragma once

#include <random>
#include <stdint.h>

class QString;

class CRandomDataGenerator
{
public:
	void setSeed(uint32_t seed);

	QString randomString(const size_t length);
	int randomInt(int min, int max);

private:
	std::mt19937 _rng;
};
