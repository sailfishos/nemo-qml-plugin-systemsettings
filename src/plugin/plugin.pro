TARGET = nemosystemsettings
PLUGIN_IMPORT_PATH = org/nemomobile/systemsettings

TEMPLATE = lib
CONFIG += qt plugin c++11 hide_symbols link_pkgconfig
QT += qml dbus network
QT -= gui

PKGCONFIG += profile usb-moded-qt5 libsailfishkeyprovider connman-qt5 packagekitqt5

target.path = $$[QT_INSTALL_QML]/$$PLUGIN_IMPORT_PATH
INSTALLS += target

qmldir.files += qmldir plugins.qmltypes
qmldir.path +=  $$[QT_INSTALL_QML]/$$$$PLUGIN_IMPORT_PATH
INSTALLS += qmldir

qmltypes.commands = qmlplugindump -nonrelocatable org.nemomobile.systemsettings 1.0 > $$PWD/plugins.qmltypes
QMAKE_EXTRA_TARGETS += qmltypes

OTHER_FILES += \
    plugins.qmltypes \
    qmldir

SOURCES += \
    plugin.cpp

INCLUDEPATH += ..
LIBS += -L.. -lsystemsettings
