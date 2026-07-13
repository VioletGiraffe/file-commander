CONFIG -= c++17
CONFIG -= c++2a
CONFIG += strict_c++ c++2b

mac* | linux* | freebsd {
	CONFIG(release, debug|release):CONFIG *= Release optimize_full
	CONFIG(debug, debug|release):CONFIG *= Debug
}

mac*{
	exists(/usr/local/bin/ccache):CONFIG += ccache
}

linux*{
	exists(/usr/bin/ccache)|exists(/usr/lib/ccache):CONFIG += ccache
}

windows*{
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
