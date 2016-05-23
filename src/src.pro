TEMPLATE = lib
TARGET = systemsettings

CONFIG += qt create_pc create_prl no_install_prl c++11
QT += qml dbus systeminfo
QT -= gui

CONFIG += c++11 hide_symbols link_pkgconfig
PKGCONFIG += profile mlite5 timed-qt5 libshadowutils blkid libcrypto

system(qdbusxml2cpp -p mceiface.h:mceiface.cpp mce.xml)

SOURCES += \
    languagemodel.cpp \
    datetimesettings.cpp \
    profilecontrol.cpp \
    alarmtonemodel.cpp \
    mceiface.cpp \
    displaysettings.cpp \
    aboutsettings.cpp \
    certificatemodel.cpp \
    devicelockiface.cpp \
    developermodesettings.cpp \
    diskusage.cpp \
    diskusage_impl.cpp \
    partition.cpp \
    partitionmanager.cpp \
    partitionmodel.cpp

PUBLIC_HEADERS = \
    languagemodel.h \
    datetimesettings.h \
    profilecontrol.h \
    alarmtonemodel.h \
    mceiface.h \
    displaysettings.h \
    aboutsettings.h \
    certificatemodel.h \
    developermodesettings.h \
    diskusage.h \
    partition.h \
    partitionmanager.h \
    partitionmodel.h \
    systemsettingsglobal.h

HEADERS += \
    $$PUBLIC_HEADERS \
    diskusage_p.h \
    partition_p.h \
    partitionmanager_p.h

DEFINES += \
    SYSTEMSETTINGS_BUILD_LIBRARY

develheaders.path = /usr/include/systemsettings
develheaders.files = $$PUBLIC_HEADERS

target.path = $$[QT_INSTALL_LIBS]
pkgconfig.files = $$PWD/pkgconfig/systemsettings.pc
pkgconfig.path = $$target.path/pkgconfig

QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_DESCRIPTION = System settings application development files
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$develheaders.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_REQUIRES = Qt5Core Qt5DBus profile

INSTALLS += target develheaders pkgconfig
