TEMPLATE = lib
TARGET = systemsettings

CONFIG += qt create_pc create_prl no_install_prl c++11
QT += qml dbus xmlpatterns
QT -= gui

CONFIG += c++11 hide_symbols link_pkgconfig
PKGCONFIG += profile mlite5 mce timed-qt5 blkid libcrypto libsailfishkeyprovider connman-qt5 glib-2.0

PKGCONFIG += ssu-sysinfo nemodbus libsystemd sailfishusermanager sailfishaccesscontrol
PKGCONFIG += qofono-qt5

system($$[QT_INSTALL_BINS]/qdbusxml2cpp -p mceiface.h:mceiface.cpp mce.xml)

SOURCES += \
    languagemodel.cpp \
    localeconfig.cpp \
    logging.cpp \
    datetimesettings.cpp \
    nfcsettings.cpp \
    profilecontrol.cpp \
    alarmtonemodel.cpp \
    mceiface.cpp \
    displaysettings.cpp \
    aboutsettings.cpp \
    certificatemodel.cpp \
    batterystatus.cpp \
    diskusage.cpp \
    diskusage_impl.cpp \
    partition.cpp \
    partitionmanager.cpp \
    partitionmodel.cpp \
    deviceinfo.cpp \
    locationsettings.cpp \
    settingsvpnmodel.cpp \
    timezoneinfo.cpp \
    udisks2block.cpp \
    udisks2blockdevices.cpp \
    udisks2job.cpp \
    udisks2monitor.cpp \
    userinfo.cpp \
    usermodel.cpp \
    permissionsmodel.cpp

PUBLIC_HEADERS = \
    languagemodel.h \
    datetimesettings.h \
    profilecontrol.h \
    alarmtonemodel.h \
    mceiface.h \
    displaysettings.h \
    aboutsettings.h \
    certificatemodel.h \
    settingsvpnmodel.h \
    batterystatus.h \
    udisks2block_p.h \
    udisks2defines.h \
    diskusage.h \
    partition.h \
    partitionmanager.h \
    partitionmodel.h \
    systemsettingsglobal.h \
    deviceinfo.h \
    locationsettings.h \
    timezoneinfo.h \
    userinfo.h \
    usermodel.h \
    permissionsmodel.h

HEADERS += \
    $$PUBLIC_HEADERS \
    aboutsettings_p.h \
    localeconfig.h \
    batterystatus_p.h \
    logging_p.h \
    diskusage_p.h \
    locationsettings_p.h \
    logging_p.h \
    nfcsettings.h \
    partition_p.h \
    partitionmanager_p.h \
    udisks2blockdevices_p.h \
    udisks2job_p.h \
    udisks2monitor_p.h \
    userinfo_p.h

CONFIG(disableDevmode) {
    warning("Developer mode plugin disabled")
} else {
    message("Developer mode plugin enabled")
    PKGCONFIG += packagekitqt5
    DEFINES += DEVELOPER_MODE_ENABLED

    SOURCES += developermodesettings.cpp
    PUBLIC_HEADERS += developermodesettings.h
}

DEFINES += \
    SYSTEMSETTINGS_BUILD_LIBRARY

develheaders.path = /usr/include/systemsettings
develheaders.files = $$PUBLIC_HEADERS

target.path = $$[QT_INSTALL_LIBS]
pkgconfig.files = $$PWD/pkgconfig/systemsettings.pc
pkgconfig.path = $$target.path/pkgconfig

locationconfig.files = $$PWD/location.conf
locationconfig.path = /var/lib/location

compat_locationconfig.files = $$PWD/location.conf
compat_locationconfig.path = /etc/location

QMAKE_PKGCONFIG_NAME = lib$$TARGET
QMAKE_PKGCONFIG_VERSION = $$VERSION
QMAKE_PKGCONFIG_DESCRIPTION = System settings application development files
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$develheaders.path
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
QMAKE_PKGCONFIG_REQUIRES = Qt5Core Qt5DBus profile libsailfishkeyprovider connman-qt5 nemodbus

INSTALLS += target develheaders pkgconfig locationconfig compat_locationconfig
