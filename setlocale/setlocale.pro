TEMPLATE = app
TARGET = setlocale
TARGETPATH = /usr/libexec
target.path = $$TARGETPATH

QT = core

CONFIG += link_pkgconfig
PKGCONFIG += sailfishaccesscontrol

SOURCES += \
    main.cpp \
    ../src/localeconfig.cpp

HEADERS += \
    ../src/localeconfig.h

INSTALLS += target
