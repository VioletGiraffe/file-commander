#pragma once

#include <limits>
#include <random>
#include <stdint.h>
#include <type_traits>

class QString;

// Test data that a seed reproduces exactly, on any platform and any standard library. That rules out
// std::uniform_int_distribution, whose mapping from the engine's output is unspecified, so the ranging is done here.
class CRandomDataGenerator
{
public:
	void setSeed(uint32_t seed);

	// Length in characters, drawn from 'A'-'Z'.
	[[nodiscard]] QString randomString(const size_t length);

	template <typename T>
	[[nodiscard]] T randomNumber(T min, T max)
	{
		static_assert(std::is_integral_v<T>, "randomNumber draws integers");

		if (max < min) [[unlikely]]
			max = min;

		using U = std::make_unsigned_t<T>;
		// Zero exactly when the request spans the whole 64 bits, the one case a raw draw already answers.
		const uint64_t span = static_cast<uint64_t>(static_cast<U>(max) - static_cast<U>(min)) + 1;
		if (span == 0) [[unlikely]]
			return static_cast<T>(nextValue());

		// The engine's range is not a whole number of spans, and folding the leftover in would make the low end of
		// the span come up oftener than the high end. Draw again instead.
		const uint64_t leftover = (uint64_t{ 0 } - span) % span; // 2^64 % span, which 64 bits cannot hold
		const uint64_t highestUnbiased = std::numeric_limits<uint64_t>::max() - leftover;

		uint64_t value = nextValue();
		while (value > highestUnbiased)
			value = nextValue();

		return static_cast<T>(static_cast<U>(static_cast<U>(min) + static_cast<U>(value % span)));
	}

private:
	// The engine's word size is 64 bits, whatever the width of the type it hands them back in.
	[[nodiscard]] uint64_t nextValue() { return static_cast<uint64_t>(_rng()); }

	std::mt19937_64 _rng;
};
