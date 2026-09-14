# Local changes to qutepart-cpp

Upstream: https://github.com/diegoiast/qutepart-cpp, commit `ce6c1b2`.

Only the syntax highlighter subset is vendored, in upstream's layout. `syntaxhighlighter.pri` is ours.

Edits on top of upstream:

1. `src/hl/language_db_generated.cpp`: `Jenkinsfile` and `#!groovy*` map to `groovy.xml`.
2. `src/hl/syntax_highlighter.h`: `SyntaxHighlighter::languageName()` added.
3. `src/hl/language_db.cpp`: `chooseLanguageXmlFileName` checks the MIME type after the file name, so the file name wins.
4. `src/hl/language_db.cpp`: the `qWarning` for an unknown indenter is commented out.
5. `qutepart-theme-data.qrc`: lists only `homunculus` and `monokai`.

To update: copy the same files from an upstream checkout, re-apply the edits above, add any new sources to `syntaxhighlighter.pri`.
