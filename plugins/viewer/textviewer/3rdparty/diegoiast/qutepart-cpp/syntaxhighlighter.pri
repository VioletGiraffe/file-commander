HEADERS += \
	$$PWD/include/qutepart/theme.h \
	$$PWD/src/context_stack.h \
	$$PWD/src/hl/context.h \
	$$PWD/src/hl/context_switcher.h \
	$$PWD/src/hl/first_chars.h \
	$$PWD/src/hl/language.h \
	$$PWD/src/hl/loader.h \
	$$PWD/src/hl/match_result.h \
	$$PWD/src/hl/rules.h \
	$$PWD/src/hl/style.h \
	$$PWD/src/hl/syntax_highlighter.h \
	$$PWD/src/hl/text_to_match.h \
	$$PWD/src/hl_factory.h \
	$$PWD/src/text_block_user_data.h

SOURCES += \
	$$PWD/src/hl/context.cpp \
	$$PWD/src/hl/context_stack.cpp \
	$$PWD/src/hl/context_switcher.cpp \
	$$PWD/src/hl/first_chars.cpp \
	$$PWD/src/hl/language.cpp \
	$$PWD/src/hl/language_db.cpp \
	$$PWD/src/hl/language_db_generated.cpp \
	$$PWD/src/hl/loader.cpp \
	$$PWD/src/hl/match_result.cpp \
	$$PWD/src/hl/rules.cpp \
	$$PWD/src/hl/style.cpp \
	$$PWD/src/hl/syntax_highlighter.cpp \
	$$PWD/src/hl/text_block_user_data.cpp \
	$$PWD/src/hl/text_to_match.cpp \
	$$PWD/src/hl_factory.cpp \
	$$PWD/src/theme.cpp

INCLUDEPATH += $$PWD/src $$PWD/include/qutepart

RESOURCES += \
	$$PWD/qutepart-syntax-files.qrc \
	$$PWD/qutepart-theme-data.qrc
