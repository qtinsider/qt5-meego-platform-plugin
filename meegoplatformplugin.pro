TARGET   = meego

TEMPLATE = lib
CONFIG += plugin

DEFINES += QT_NO_FOREACH

QT += \
    core-private gui-private \
    service_support-private theme_support-private \
    fontdatabase_support-private xkbcommon_support-private

qtHaveModule(linuxaccessibility_support-private): \
    QT += linuxaccessibility_support-private

qtConfig(glib) : QMAKE_USE_PRIVATE += glib

XCB_LIBDIR = $$PWD/xcb
INCLUDEPATH += $$XCB_LIBDIR/include $$XCB_LIBDIR/sysinclude
INCLUDEPATH += $$XCB_LIBDIR/include/xcb

LIBS += $$OUT_PWD/xcb/libxcb-static.a

SOURCES = \
        qxcbmain.cpp \
        qxcbclipboard.cpp \
        qxcbconnection.cpp \
        qxcbintegration.cpp \
        qxcbkeyboard.cpp \
        qxcbmime.cpp \
        qxcbscreen.cpp \
        qxcbwindow.cpp \
        qxcbbackingstore.cpp \
        qxcbwmsupport.cpp \
        qxcbnativeinterface.cpp \
        qxcbcursor.cpp \
        qxcbimage.cpp \
        qxcbxsettings.cpp \
        qxcbeventqueue.cpp \
        qxcbeventdispatcher.cpp \
        qxcbconnection_basic.cpp \
        qxcbconnection_xi2.cpp \
        qxcbconnection_screens.cpp \
        qxcbatom.cpp \
        qxcbsessionmanager.cpp

HEADERS = \
        qxcbclipboard.h \
        qxcbconnection.h \
        qxcbintegration.h \
        qxcbkeyboard.h \
        qxcbmime.h \
        qxcbexport.h \
        qxcbobject.h \
        qxcbscreen.h \
        qxcbwindow.h \
        qxcbbackingstore.h \
        qxcbwmsupport.h \
        qxcbnativeinterface.h \
        qxcbcursor.h \
        qxcbimage.h \
        qxcbxsettings.h \
        qxcbeventqueue.h \
        qxcbeventdispatcher.h \
        qxcbconnection_basic.h \
        qxcbatom.h \
        qxcbsessionmanager.h

qtConfig(xcb-xlib) {
    QMAKE_USE += xcb_xlib
}

QMAKE_USE += xkbcommon xkbcommon_x11

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = MeegoIntegrationPlugin

DISTFILES += \
    meego.json
