QT       += core gui widgets charts network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = VC3DProfileViewer
TEMPLATE = app
CONFIG  += c++17

# Windows: no console window in release build
win32:CONFIG(release, debug|release): CONFIG += windows
win32:CONFIG(debug,   debug|release): CONFIG += console

INCLUDEPATH += include

SOURCES += \
    src/main.cpp             \
    src/mainwindow.cpp       \
    src/profilewidget.cpp    \
    src/sensorworker.cpp     \
    src/vcprotocol.cpp       \
    src/jsonplayer.cpp

HEADERS += \
    include/mainwindow.h     \
    include/profilewidget.h  \
    include/sensorworker.h   \
    include/vcprotocol.h     \
    include/jsonplayer.h

# ── Deployment: create portable folders next to EXE ─────────────────────────
# After building, run from the EXE directory:
#   windeployqt VC3DProfileViewer.exe
# That copies all required Qt DLLs automatically.

# Output dirs
CONFIG(debug, debug|release) {
    DESTDIR = $$OUT_PWD/bin/Debug
} else {
    DESTDIR = $$OUT_PWD/bin/Release
}
OBJECTS_DIR = $$OUT_PWD/.obj
MOC_DIR     = $$OUT_PWD/.moc
RCC_DIR     = $$OUT_PWD/.rcc
UI_DIR      = $$OUT_PWD/.ui
