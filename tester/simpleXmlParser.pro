
CONFIG += debug debug_and_release
TARGET = xmlparsetest
DEPENDPATH += . paramparser_class ../simplexmlparser_class
INCLUDEPATH += . paramparser_class ../simplexmlparser_class

# Input
HEADERS += paramparser_class/nrparamparser.h \
           ../simplexmlparser_class/SimpleXmlParser.h
SOURCES += main.cpp \
           paramparser_class/nrparamparser.cpp \
           ../simplexmlparser_class/SimpleXmlParser.cpp

unix {
TEMPLATE = app
# Workaround for Qt 6.8.x + Xcode 26 SDK: __yield not declared in qyieldcpu.h
QMAKE_CXXFLAGS += -Wno-implicit-function-declaration
}

win32 {
TEMPLATE = app
}
