# SPDX-License-Identifier: AGPL-3.0-only
# Whether the sound is in headphones, without the bus. Built and run by scripts/test-mediaroute.sh.
TARGET = mediaroute-test
QT = core dbus
CONFIG += c++14 console
INCLUDEPATH += ../../bridge
SOURCES += main.cpp ../../bridge/mediaroute.cpp
HEADERS += ../../bridge/mediaroute.h
