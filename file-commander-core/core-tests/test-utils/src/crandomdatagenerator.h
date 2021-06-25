#pragma once
#include "utility/template_magic.hpp"

#include <random>
#include <stdint.h>

class QString;

class CRandomDataGenerator
{
public:
	void setSeed(uint32_t seed);

	[[nodiscard]] QString randomString(const size_t length);

	template <typename T>
	[[nodiscard]] T randomNumber(T min, T max)
	{
		if constexpr (std::is_integral_v<T>)
			return std::uniform_int_distribution<T>(min, max)(_rng);
		else if constexpr (std::is_floating_point_v<T>)
			return std::uniform_real_distribution<T>(min, max)(_rng);
		else
		{
			static_assert(false_v<T>);
			return {};
		}
	}

private:
	std::mt19937 _rng;
};
