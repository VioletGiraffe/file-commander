HEADERS += \
	$$PWD/bracket_highlighter.h \
	$$PWD/char_iterator.h \
	$$PWD/context_stack.h \
	$$PWD/hl/context.h \
	$$PWD/hl/context_switcher.h \
	$$PWD/hl/language.h \
	$$PWD/hl/loader.h \
	$$PWD/hl/match_result.h \
	$$PWD/hl/rules.h \
	$$PWD/hl/style.h \
	$$PWD/hl/syntax_highlighter.h \
	$$PWD/hl/text_to_match.h \
	$$PWD/hl/text_type.h \
	$$PWD/hl_factory.h \
	$$PWD/indent/alg_cstyle.h \
	$$PWD/indent/alg_impl.h \
	$$PWD/indent/alg_lisp.h \
	$$PWD/indent/alg_python.h \
	$$PWD/indent/alg_ruby.h \
	$$PWD/indent/alg_scheme.h \
	$$PWD/indent/alg_xml.h \
	$$PWD/indent/indent_funcs.h \
	$$PWD/indent/indenter.h \
	$$PWD/text_block_flags.h \
	$$PWD/text_block_user_data.h \
	$$PWD/text_block_utils.h \
	$$PWD/text_pos.h \
	$$PWD/theme.h

SOURCES += \
	$$PWD/bracket_highlighter.cpp \
	$$PWD/char_iterator.cpp \
	$$PWD/hl/context.cpp \
	$$PWD/hl/context_stack.cpp \
	$$PWD/hl/context_switcher.cpp \
	$$PWD/hl/language.cpp \
	$$PWD/hl/language_db.cpp \
	$$PWD/hl/language_db_generated.cpp \
	$$PWD/hl/loader.cpp \
	$$PWD/hl/match_result.cpp \
	$$PWD/hl/rules.cpp \
	$$PWD/hl/style.cpp \
	$$PWD/hl/syntax_highlighter.cpp \
	$$PWD/hl/text_block_user_data.cpp \
	$$PWD/hl/text_to_match.cpp \
	$$PWD/hl/text_type.cpp \
	$$PWD/hl_factory.cpp \
	$$PWD/indent/alg_cstyle.cpp \
	$$PWD/indent/alg_impl.cpp \
	$$PWD/indent/alg_lisp.cpp \
	$$PWD/indent/alg_python.cpp \
	$$PWD/indent/alg_ruby.cpp \
	$$PWD/indent/alg_scheme.cpp \
	$$PWD/indent/alg_xml.cpp \
	$$PWD/indent/indent_funcs.cpp \
	$$PWD/indent/indenter.cpp \
	$$PWD/text_block_flags.cpp \
	$$PWD/text_block_utils.cpp \
	$$PWD/theme.cpp

INCLUDEPATH += $$PWD/

RESOURCES += \
	$$PWD/qutepart-syntax-files.qrc \
	$$PWD/qutepart-theme-data.qrc
