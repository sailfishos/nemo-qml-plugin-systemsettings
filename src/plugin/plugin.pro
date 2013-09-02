TARGET = nemosystemsettings
PLUGIN_IMPORT_PATH = org/nemomobile/systemsettings

TEMPLATE = lib
CONFIG += qt plugin hide_symbols link_pkgconfig
QT += qml dbus network
QT -= gui

PKGCONFIG += qmsystem2-qt5 profile

target.path = $$[QT_INSTALL_QML]/$$PLUGIN_IMPORT_PATH
INSTALLS += target

qmldir.files += $$_PRO_FILE_PWD_/qmldir
qmldir.path +=  $$[QT_INSTALL_QML]/$$$$PLUGIN_IMPORT_PATH
INSTALLS += qmldir

OTHER_FILES += \
    qmldir

SOURCES += \
    plugin.cpp

INCLUDEPATH += ..
LIBS += -L.. -lsystemsettings
