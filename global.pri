CONFIG -= c++17
CONFIG -= c++2a
CONFIG += strict_c++ c++2b

mac* | linux* | freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

mac*{
	exists(/usr/local/bin/ccache):CONFIG += ccache

	QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.3
}

linux*{
	exists(/usr/bin/ccache)|exists(/usr/lib/ccache):CONFIG += ccache
}

win*{
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
	QMAKE_CXXFLAGS += /utf-8

	Release:QMAKE_CXXFLAGS += /GL
	Release:QMAKE_LFLAGS += /DEBUG:FULL /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
}

linux*:Release {
	QMAKE_CXXFLAGS += -flto=auto -ffat-lto-objects
	QMAKE_CFLAGS   += -flto=auto -ffat-lto-objects
	QMAKE_LFLAGS   += -flto=auto
}

mac*:Release {
	QMAKE_CXXFLAGS += -flto=thin
	QMAKE_CFLAGS   += -flto=thin
	QMAKE_LFLAGS   += -flto=thin
}

linux*|mac*|freebsd{
	QMAKE_CXXFLAGS += -pedantic-errors
	QMAKE_CFLAGS += -pedantic-errors

	QMAKE_CXXFLAGS_WARN_ON *= -Wall -Wextra -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-qual -Wdouble-promotion -Wfloat-conversion -Wundef
	QMAKE_CXXFLAGS_WARN_ON *= -Wformat=2 -Wextra-semi -Wzero-as-null-pointer-constant -Wfloat-equal -Wredundant-decls -Wvla

	QMAKE_CXXFLAGS *= -Werror=return-type -Werror=uninitialized -Werror=delete-non-virtual-dtor -Werror=address
	QMAKE_CXXFLAGS *= -Werror=sizeof-pointer-div -Werror=sizeof-pointer-memaccess

	contains(QMAKE_COMPILER, clang) {
		QMAKE_CXXFLAGS_WARN_ON *= -Wshadow-all -Wcast-align -Wcomma -Wconditional-uninitialized -Wheader-hygiene -Wloop-analysis -Wextra-semi-stmt -Wunreachable-code-aggressive
		QMAKE_CXXFLAGS_WARN_ON *= -Wshorten-64-to-32 -Wmissing-prototypes -Wmissing-variable-declarations
		QMAKE_CXXFLAGS_WARN_ON *= -Wimplicit-fallthrough -Wsuggest-override
		QMAKE_CXXFLAGS *= -Werror=return-stack-address -Werror=infinite-recursion
	} else {
		QMAKE_CXXFLAGS_WARN_ON *= -Wshadow -Wcast-align=strict -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference
		QMAKE_CXXFLAGS_WARN_ON *= -Wsuggest-override -Wmissing-declarations -Wmismatched-tags -Wunused-const-variable=1
		QMAKE_CXXFLAGS *= -Werror=return-local-addr -Werror=memset-transposed-args -Werror=nonnull-compare -Werror=mismatched-new-delete -Werror=infinite-recursion
		QMAKE_CXXFLAGS *= -Wcatch-value=3 -Werror=catch-value # -Werror=catch-value on its own would only enable level 1
	}

	Release:DEFINES += NDEBUG=1
	Debug:DEFINES += _DEBUG
}

