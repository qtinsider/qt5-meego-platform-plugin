TARGET   = meego

TEMPLATE = lib
CONFIG += plugin

DEFINES += QT_NO_FOREACH

QT += \
    core-private gui-private \
    service_support-private theme_support-private \
    fontdatabase_support-private

QMAKE_USE_PRIVATE += glib

XCB_LIBDIR = $$PWD/xcb
INCLUDEPATH += $$XCB_LIBDIR/include $$XCB_LIBDIR/sysinclude
INCLUDEPATH += $$XCB_LIBDIR/include/xcb

LIBS += $$OUT_PWD/xcb/libxcb-static.a /opt/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim/usr/local/lib/libxcb.a # TODO: Remove

SOURCES = \
        main.cpp \
        meventdispatcher.cpp \
        mplatformbackingstore.cpp \
        mplatformclipboard.cpp \
        mplatformintegration.cpp \
        mplatformnativeinterface.cpp \
        mplatformscreen.cpp \
        mplatformsessionmanager.cpp \
        mplatformwindow.cpp \
        xcbatom.cpp \
        xcbconnection.cpp \
        xcbconnection_basic.cpp \
        xcbconnection_xi2.cpp \
        xcbeventqueue.cpp \
        xcbimage.cpp \
        xcbmime.cpp \
        xcbwmsupport.cpp

HEADERS = \
        meventdispatcher.h \
        mexport.h \
        mplatformbackingstore.h \
        mplatformclipboard.h \
        mplatformintegration.h \
        mplatformnativeinterface.h \
        mplatformscreen.h \
        mplatformsessionmanager.h \
        mplatformwindow.h \
        xcbatom.h \
        xcbconnection.h \
        xcbconnection_basic.h \
        xcbeventqueue.h \
        xcbimage.h \
        xcbmime.h \
        xcbobject.h \
        xcbwmsupport.h

QMAKE_USE += xcb_xlib

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = MeegoIntegrationPlugin

DISTFILES += \
    LICENSE README meego.json
