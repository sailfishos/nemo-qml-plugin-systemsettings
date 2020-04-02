TEMPLATE = subdirs

src_plugins.subdir = src/plugin
src_plugins.target = sub-plugins
src_plugins.depends = src

OTHER_FILES += rpm/nemo-qml-plugin-systemsettings.spec

SUBDIRS = src src_plugins setlocale tests translations
