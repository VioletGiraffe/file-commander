#pragma once

#include <iostream>

class QString;

std::ostream& operator<<(std::ostream& stream, const QString& qString);
QString qStringFromWstring(const std::wstring& ws);

struct Logger {
	template <typename T>
	std::ostream& operator<<(const T item) {
		return std::cout << item;
	}

	inline ~Logger() {
		std::cout << std::endl;
	}
};
