macos {
    include(macos/macos.pri)
}

ios {
    # include(ios/ios.pri)
}

QMAKE_CXXFLAGS += -Wno-deprecated-copy
INCLUDEPATH += $$PWD
