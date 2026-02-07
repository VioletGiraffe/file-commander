CONFIG -= c++17
CONFIG -= c++2a
CONFIG += strict_c++ c++2b

mac*{
	#exists(/usr/local/bin/ccache):CONFIG += ccache
}

linux*{
	exists(/usr/bin/ccache)|exists(/usr/lib/ccache):CONFIG += ccache
}
