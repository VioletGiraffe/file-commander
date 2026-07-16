#pragma once
#include "compiler/compiler_warnings_control.h"
#include "hash/wheathash.hpp"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

struct IdentityHash {
	using is_avalanching = void;

	[[nodiscard]] inline constexpr qulonglong operator()(qulonglong hashValue) const noexcept {
		return hashValue;
	}
};

struct IdentityHashExtraMixing {
	[[nodiscard]] inline constexpr qulonglong operator()(qulonglong hashValue) const noexcept {
		return hashValue;
	}
};

struct QStringHash {
	using is_avalanching = void;
	[[nodiscard]] inline size_t operator()(const QString& s) const noexcept {
		return ::wheathash64(s.constData(), s.size() * sizeof(QChar));
	}
};
