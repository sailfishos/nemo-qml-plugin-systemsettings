TARGET = nemosystemsettings
PLUGIN_IMPORT_PATH = org/nemomobile/systemsettings

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

