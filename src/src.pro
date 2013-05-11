TARGET = nemosystemsettings
PLUGIN_IMPORT_PATH = org/nemomobile/systemsettings

TEMPLATE = lib
CONFIG += qt plugin hide_symbols
QT += declarative

target.path = $$[QT_INSTALL_IMPORTS]/$$PLUGIN_IMPORT_PATH
INSTALLS += target

qmldir.files += $$_PRO_FILE_PWD_/qmldir
qmldir.path +=  $$[QT_INSTALL_IMPORTS]/$$$$PLUGIN_IMPORT_PATH
INSTALLS += qmldir

QT += dbus
CONFIG += qmsystem2 mobility link_pkgconfig
PKGCONFIG += profile
MOBILITY += systeminfo

OTHER_FILES += \
    qmldir

system(qdbusxml2cpp -p mceiface.h:mceiface.cpp mce.xml)

SOURCES += \
    plugin.cpp \
    languagemodel.cpp \
    datetimesettings.cpp \
    profilecontrol.cpp \
    alarmtonemodel.cpp \
    mceiface.cpp \
    displaysettings.cpp \
    usbsettings.cpp \
    aboutsettings.cpp

HEADERS += \
    languagemodel.h \
    datetimesettings.h \
    profilecontrol.h \
    alarmtonemodel.h \
    mceiface.h \
    displaysettings.h \
    usbsettings.h \
    aboutsettings.h

include(../../plugin.pri)

