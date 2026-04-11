#-------------------------------------------------
# Unit tests for parseMediaInfo() in ../mediainfo.cpp.
#
# Builds a standalone console binary using QtTest. The source files from
# the main project live one directory up; INCLUDEPATH handles the header
# and SOURCES references the pure-function translation unit directly so
# we don't need to link against the full cube binary.
#-------------------------------------------------

QT        = core testlib
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE  = app
TARGET    = tst_mediainfo

QMAKE_CXXFLAGS = -Wno-unused-parameter

INCLUDEPATH += ..

SOURCES += \
        tst_mediainfo.cpp \
        ../mediainfo.cpp

HEADERS += \
        ../mediainfo.h
