TEMPLATE = lib
TARGET = systemsettings

# TODO: hide_symbols
CONFIG += qt create_pc create_prl no_install_prl
QT += qml dbus systeminfo

CONFIG += link_pkgconfig
PKGCONFIG += qmsystem2-qt5 profile

system(qdbusxml2cpp -p mceiface.h:mceiface.cpp mce.xml)

SOURCES += \
    languagemodel.cpp \
    datetimesettings.cpp \
    profilecontrol.cpp \
    alarmtonemodel.cpp \
    mceiface.cpp \
    displaysettings.cpp \
    usbsettings.cpp \
    aboutsettings.cpp \
    devicelockiface.cpp

HEADERS += \
    languagemodel.h \
    datetimesettings.h \
    profilecontrol.h \
    alarmtonemodel.h \
    mceiface.h \
    displaysettings.h \
    usbsettings.h \
    aboutsettings.h \
    devicelockiface.h

develheaders.path = /usr/include/systemsettings
develheaders.files = $$HEADERS

target.path = $$[QT_INSTALL_LIBS]
pkgconfig.files = $$PWD/pkgconfig/systemsettings.pc
pkgconfig.path = $$target.path/pkgconfig

QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = System settings application development files
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$develheaders.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_REQUIRES = Qt5Core Qt5DBus profile qmsystem2-qt5

INSTALLS += target develheaders pkgconfig
