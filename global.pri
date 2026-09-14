CONFIG -= c++17
CONFIG -= c++2a
CONFIG += strict_c++ c++2b

mac* | linux* | freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

Release:DEFINES += NDEBUG=1
Debug:DEFINES += _DEBUG

mac*{
	exists(/usr/local/bin/ccache)|exists(/opt/homebrew/bin/ccache):CONFIG += ccache

	QMAKE_MACOSX_DEPLOYMENT_TARGET = 13.3

	contains(QMAKE_HOST.arch, arm64)|contains(QMAKE_APPLE_DEVICE_ARCHS, arm64) {
		QMAKE_CXXFLAGS += -include arm_acle.h
	}
}

linux*{
	exists(/usr/bin/ccache)|exists(/usr/lib/ccache):CONFIG += ccache
}

win*{
	DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX _SCL_SECURE_NO_WARNINGS
	QMAKE_CXXFLAGS += /MP /Zi /FS /utf-8 /wd4251
	QMAKE_CXXFLAGS += /std:c++latest /permissive- /Zc:__cplusplus
	QMAKE_CXXFLAGS_WARN_ON = /W4
	QMAKE_CXXFLAGS += /we4715 /we4716 # not all control paths return a value / must return a value
	QMAKE_CXXFLAGS += /we4172         # returning address of local variable or temporary
	QMAKE_CXXFLAGS += /we4700         # uninitialized local variable used
	QMAKE_CXXFLAGS += /we4477         # printf format string does not match the argument
	QMAKE_CXXFLAGS += /we4551         # function call missing argument list
	QMAKE_CXXFLAGS += /we4552 /we4553 # operator has no effect; did you intend '='?
	QMAKE_LFLAGS += /DEBUG

	Debug:QMAKE_CXXFLAGS += /JMC

	Release:QMAKE_CXXFLAGS += /GL
	Release:QMAKE_LFLAGS += /OPT:REF /OPT:ICF /TIME /LTCG:INCREMENTAL
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
	QMAKE_CXXFLAGS_WARN_ON *= -Wno-missing-field-initializers

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
}

