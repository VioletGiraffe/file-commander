#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QHash>
#include <QString>
RESTORE_COMPILER_WARNINGS

struct NullHash {
	using is_avalanching = void;

	[[nodiscard]] inline constexpr qulonglong operator()(qulonglong hashValue) const noexcept {
		return hashValue;
	}
};

struct NullHashExtraMixing {
	[[nodiscard]] inline constexpr qulonglong operator()(qulonglong hashValue) const noexcept {
		return hashValue;
	}
};

struct QStringHash {
	using is_avalanching = void;
	[[nodiscard]] inline size_t operator()(const QString& s) const noexcept {
		return qHash(s);
	}
};
