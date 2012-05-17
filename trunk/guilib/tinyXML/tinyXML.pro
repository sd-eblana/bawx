include(../../boxee.pri)

TEMPLATE = lib
CONFIG += lib_bundle staticlib
CONFIG -= app_bundle
QT += core gui opengl

MOC_DIR=build
OBJECTS_DIR=build
INCLUDEPATH += . ../ ../../xbmc ../../xbmc/linux ../../xbmc/utils
SOURCES += \
tinyxmlparser.cpp \
tinyxmlerror.cpp \
tinystr.cpp \
tinyxml.cpp \


HEADERS += \
tinystr.h \
tinyxml.h \


