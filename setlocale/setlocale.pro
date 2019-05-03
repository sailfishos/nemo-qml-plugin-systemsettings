TEMPLATE = app
TARGET = setlocale
TARGETPATH = /usr/libexec
target.path = $$TARGETPATH

QT = core

SOURCES += \
    main.cpp \
    ../src/localeconfig.cpp

HEADERS += \
    ../src/localeconfig.h

INSTALLS += target
