# based on tests.pro from libprofile-qt

PACKAGENAME = nemo-qml-plugin-systemsettings

QT += testlib qml dbus systeminfo
QT -= gui

system(sed -e s/@PACKAGENAME@/$${PACKAGENAME}/g tests.xml.template > tests.xml)

TEMPLATE = app
TARGET = ut_diskusage

target.path = $$[QT_INSTALL_LIBS]/$${PACKAGENAME}-tests

xml.path = /usr/share/$${PACKAGENAME}-tests
xml.files = tests.xml

contains(cov, true) {
    message("Coverage options enabled")
    QMAKE_CXXFLAGS += --coverage
    QMAKE_LFLAGS += --coverage
}

CONFIG += link_prl
DEFINES += UNIT_TEST
QMAKE_EXTRA_TARGETS = check

check.depends = $$TARGET
check.commands = LD_LIBRARY_PATH=../../lib ./$$TARGET

INCLUDEPATH += ../src/

SOURCES += ut_diskusage.cpp
HEADERS += ut_diskusage.h

SOURCES += ../src/diskusage.cpp
HEADERS += ../src/diskusage.h
HEADERS += ../src/diskusage_p.h

INSTALLS += target xml
