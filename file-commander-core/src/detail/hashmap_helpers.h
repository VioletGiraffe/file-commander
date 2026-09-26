#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/wheathash.hpp"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringView>
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

// Transparent: with std::equal_to<> as the map's equality, a QString key can be found by a QStringView
struct QStringHash {
	using is_avalanching = void;
	using is_transparent = void;
	[[nodiscard]] inline size_t operator()(QStringView s) const noexcept {
		return ::wheathash64(s.constData(), s.size() * sizeof(QChar));
	}
};
