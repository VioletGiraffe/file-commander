# Terminal UI prototype. Links the libraries the main file-commander.pro builds into bin/, so that build must run first.
TEMPLATE = subdirs

SUBDIRS = tvision app
app.depends = tvision
