/*
 * Copyright (C) 2020 Chukwudi Nwutobo <nwutobo@outlook.com>
 * Copyright (C) 2018 The Qt Company Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mplatformnativeinterface.h"

#include "mplatformscreen.h"
#include "mplatformwindow.h"
#include "mplatformintegration.h"

#include <private/qguiapplication_p.h>
#include <QtCore/QMap>

#include <QtCore/QDebug>

#include <QtGui/qopenglcontext.h>
#include <QtGui/qscreen.h>

#include <stdio.h>

#include <algorithm>

// return MNativeInterface::ResourceType for the key.
static int resourceType(const QByteArray &key)
{
    static const QByteArray names[] = {
        // match MNativeInterface::ResourceType
        QByteArrayLiteral("display"),
        QByteArrayLiteral("connection"),
        QByteArrayLiteral("screen"),
        QByteArrayLiteral("apptime"),
        QByteArrayLiteral("appusertime"),
        QByteArrayLiteral("startupid"),
        QByteArrayLiteral("gettimestamp"),
        QByteArrayLiteral("rootwindow"),
        QByteArrayLiteral("generatepeekerid"),
        QByteArrayLiteral("removepeekerid"),
        QByteArrayLiteral("peekeventqueue")
    };
    const QByteArray *end = names + sizeof(names) / sizeof(names[0]);
    const QByteArray *result = std::find(names, end, key);
    return int(result - names);
}

MPlatformNativeInterface::MPlatformNativeInterface()
{
}

void *MPlatformNativeInterface::nativeResourceForIntegration(const QByteArray &resourceString)
{
    QByteArray lowerCaseResource = resourceString.toLower();
    void *result = handlerNativeResourceForIntegration(lowerCaseResource);
    if (result)
        return result;

    switch (resourceType(lowerCaseResource)) {
    case StartupId:
        result = startupId();
        break;
    case RootWindow:
        result = rootWindow();
        break;
    case Display:
        result = display();
        break;
    case Connection:
        result = connection();
        break;
    default:
        break;
    }

    return result;
}

void *MPlatformNativeInterface::nativeResourceForContext(const QByteArray &resourceString, QOpenGLContext *context)
{
    QByteArray lowerCaseResource = resourceString.toLower();
    void *result = handlerNativeResourceForContext(lowerCaseResource, context);
    return result;
}

void *MPlatformNativeInterface::nativeResourceForScreen(const QByteArray &resourceString, QScreen *screen)
{
    if (!screen) {
        qWarning("nativeResourceForScreen: null screen");
        return nullptr;
    }

    QByteArray lowerCaseResource = resourceString.toLower();
    void *result = handlerNativeResourceForScreen(lowerCaseResource, screen);
    if (result)
        return result;

    const MPlatformScreen *xcbScreen = static_cast<MPlatformScreen *>(screen->handle());
    switch (resourceType(lowerCaseResource)) {
    case Display:
        result = xcbScreen->connection()->xlib_display();
        break;
    case AppTime:
        result = appTime(xcbScreen);
        break;
    case AppUserTime:
        result = appUserTime(xcbScreen);
        break;
    case GetTimestamp:
        result = getTimestamp(xcbScreen);
        break;
    case RootWindow:
        result = reinterpret_cast<void *>(xcbScreen->root());
        break;
    default:
        break;
    }
    return result;
}

void *MPlatformNativeInterface::nativeResourceForWindow(const QByteArray &resourceString, QWindow *window)
{
    QByteArray lowerCaseResource = resourceString.toLower();
    void *result = handlerNativeResourceForWindow(lowerCaseResource, window);
    if (result)
        return result;

    switch (resourceType(lowerCaseResource)) {
    case Display:
        result = displayForWindow(window);
        break;
    case Connection:
        result = connectionForWindow(window);
        break;
    case Screen:
        result = screenForWindow(window);
        break;
    default:
        break;
    }

    return result;
}

void *MPlatformNativeInterface::nativeResourceForBackingStore(const QByteArray &resourceString, QBackingStore *backingStore)
{
    const QByteArray lowerCaseResource = resourceString.toLower();
    void *result = handlerNativeResourceForBackingStore(lowerCaseResource,backingStore);
    return result;
}

QPlatformNativeInterface::NativeResourceForIntegrationFunction MPlatformNativeInterface::nativeResourceFunctionForIntegration(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();
    QPlatformNativeInterface::NativeResourceForIntegrationFunction func = handlerNativeResourceFunctionForIntegration(lowerCaseResource);
    if (func)
        return func;

    if (lowerCaseResource == "setstartupid")
        return NativeResourceForIntegrationFunction(reinterpret_cast<void *>(setStartupId));
    if (lowerCaseResource == "generatepeekerid")
        return NativeResourceForIntegrationFunction(reinterpret_cast<void *>(generatePeekerId));
    if (lowerCaseResource == "removepeekerid")
        return NativeResourceForIntegrationFunction(reinterpret_cast<void *>(removePeekerId));
    if (lowerCaseResource == "peekeventqueue")
        return NativeResourceForIntegrationFunction(reinterpret_cast<void *>(peekEventQueue));

    return nullptr;
}

QPlatformNativeInterface::NativeResourceForContextFunction MPlatformNativeInterface::nativeResourceFunctionForContext(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();
    QPlatformNativeInterface::NativeResourceForContextFunction func = handlerNativeResourceFunctionForContext(lowerCaseResource);
    if (func)
        return func;
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForScreenFunction MPlatformNativeInterface::nativeResourceFunctionForScreen(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();
    NativeResourceForScreenFunction func = handlerNativeResourceFunctionForScreen(lowerCaseResource);
    if (func)
        return func;

    if (lowerCaseResource == "setapptime")
        return NativeResourceForScreenFunction(reinterpret_cast<void *>(setAppTime));
    else if (lowerCaseResource == "setappusertime")
        return NativeResourceForScreenFunction(reinterpret_cast<void *>(setAppUserTime));
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForWindowFunction MPlatformNativeInterface::nativeResourceFunctionForWindow(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();
    NativeResourceForWindowFunction func = handlerNativeResourceFunctionForWindow(lowerCaseResource);
    return func;
}

QPlatformNativeInterface::NativeResourceForBackingStoreFunction MPlatformNativeInterface::nativeResourceFunctionForBackingStore(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();
    NativeResourceForBackingStoreFunction func = handlerNativeResourceFunctionForBackingStore(lowerCaseResource);
    return func;
}

QFunctionPointer MPlatformNativeInterface::platformFunction(const QByteArray &function) const
{
    const QByteArray lowerCaseFunction = function.toLower();
    QFunctionPointer func = handlerPlatformFunction(lowerCaseFunction);
    if (func)
        return func;

    return nullptr;
}

void *MPlatformNativeInterface::appTime(const MPlatformScreen *screen)
{
    if (!screen)
        return nullptr;

    return reinterpret_cast<void *>(quintptr(screen->connection()->time()));
}

void *MPlatformNativeInterface::appUserTime(const MPlatformScreen *screen)
{
    if (!screen)
        return nullptr;

    return reinterpret_cast<void *>(quintptr(screen->connection()->netWmUserTime()));
}

void *MPlatformNativeInterface::getTimestamp(const MPlatformScreen *screen)
{
    if (!screen)
        return nullptr;

    return reinterpret_cast<void *>(quintptr(screen->connection()->getTimestamp()));
}

void *MPlatformNativeInterface::startupId()
{
    MPlatformIntegration* integration = MPlatformIntegration::instance();
    XcbConnection *defaultConnection = integration->defaultConnection();
    if (defaultConnection)
        return reinterpret_cast<void *>(const_cast<char *>(defaultConnection->startupId().constData()));
    return nullptr;
}

void *MPlatformNativeInterface::rootWindow()
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    XcbConnection *defaultConnection = integration->defaultConnection();
    if (defaultConnection)
        return reinterpret_cast<void *>(defaultConnection->rootWindow());
    return nullptr;
}

void *MPlatformNativeInterface::display()
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    XcbConnection *defaultConnection = integration->defaultConnection();
    if (defaultConnection)
        return defaultConnection->xlib_display();
    return nullptr;
}

void *MPlatformNativeInterface::connection()
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    return integration->defaultConnection()->xcb_connection();
}

void MPlatformNativeInterface::setAppTime(QScreen* screen, xcb_timestamp_t time)
{
    if (screen) {
        static_cast<MPlatformScreen *>(screen->handle())->connection()->setTime(time);
    }
}

void MPlatformNativeInterface::setAppUserTime(QScreen* screen, xcb_timestamp_t time)
{
    if (screen) {
        static_cast<MPlatformScreen *>(screen->handle())->connection()->setNetWmUserTime(time);
    }
}

qint32 MPlatformNativeInterface::generatePeekerId()
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    return integration->defaultConnection()->eventQueue()->generatePeekerId();
}

bool MPlatformNativeInterface::removePeekerId(qint32 peekerId)
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    return integration->defaultConnection()->eventQueue()->removePeekerId(peekerId);
}

bool MPlatformNativeInterface::peekEventQueue(XcbEventQueue::PeekerCallback peeker, void *peekerData,
                                         XcbEventQueue::PeekOptions option, qint32 peekerId)
{
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    return integration->defaultConnection()->eventQueue()->peekEventQueue(peeker, peekerData, option, peekerId);
}

void MPlatformNativeInterface::setStartupId(const char *data)
{
    QByteArray startupId(data);
    MPlatformIntegration *integration = MPlatformIntegration::instance();
    XcbConnection *defaultConnection = integration->defaultConnection();
    if (defaultConnection)
        defaultConnection->setStartupId(startupId);
}

MPlatformScreen *MPlatformNativeInterface::qPlatformScreenForWindow(QWindow *window)
{
    MPlatformScreen *screen;
    if (window) {
        QScreen *qs = window->screen();
        screen = static_cast<MPlatformScreen *>(qs ? qs->handle() : nullptr);
    } else {
        QScreen *qs = QGuiApplication::primaryScreen();
        screen = static_cast<MPlatformScreen *>(qs ? qs->handle() : nullptr);
    }
    return screen;
}

void *MPlatformNativeInterface::displayForWindow(QWindow *window)
{
    MPlatformScreen *screen = qPlatformScreenForWindow(window);
    return screen ? screen->connection()->xlib_display() : nullptr;
}

void *MPlatformNativeInterface::connectionForWindow(QWindow *window)
{
    MPlatformScreen *screen = qPlatformScreenForWindow(window);
    return screen ? screen->xcb_connection() : nullptr;
}

void *MPlatformNativeInterface::screenForWindow(QWindow *window)
{
    MPlatformScreen *screen = qPlatformScreenForWindow(window);
    return screen ? screen->screen() : nullptr;
}

QPlatformNativeInterface::NativeResourceForIntegrationFunction MPlatformNativeInterface::handlerNativeResourceFunctionForIntegration(const QByteArray &resource) const
{
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForContextFunction MPlatformNativeInterface::handlerNativeResourceFunctionForContext(const QByteArray &resource) const
{
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForScreenFunction MPlatformNativeInterface::handlerNativeResourceFunctionForScreen(const QByteArray &resource) const
{
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForWindowFunction MPlatformNativeInterface::handlerNativeResourceFunctionForWindow(const QByteArray &resource) const
{
    return nullptr;
}

QPlatformNativeInterface::NativeResourceForBackingStoreFunction MPlatformNativeInterface::handlerNativeResourceFunctionForBackingStore(const QByteArray &resource) const
{
    return nullptr;
}

QFunctionPointer MPlatformNativeInterface::handlerPlatformFunction(const QByteArray &function) const
{
    return nullptr;
}

void *MPlatformNativeInterface::handlerNativeResourceForIntegration(const QByteArray &resource) const
{
    NativeResourceForIntegrationFunction func = handlerNativeResourceFunctionForIntegration(resource);
    if (func)
        return func();
    return nullptr;
}

void *MPlatformNativeInterface::handlerNativeResourceForContext(const QByteArray &resource, QOpenGLContext *context) const
{
    NativeResourceForContextFunction func = handlerNativeResourceFunctionForContext(resource);
    if (func)
        return func(context);
    return nullptr;
}

void *MPlatformNativeInterface::handlerNativeResourceForScreen(const QByteArray &resource, QScreen *screen) const
{
    NativeResourceForScreenFunction func = handlerNativeResourceFunctionForScreen(resource);
    if (func)
        return func(screen);
    return nullptr;
}

void *MPlatformNativeInterface::handlerNativeResourceForWindow(const QByteArray &resource, QWindow *window) const
{
    NativeResourceForWindowFunction func = handlerNativeResourceFunctionForWindow(resource);
    if (func)
        return func(window);
    return nullptr;
}

void *MPlatformNativeInterface::handlerNativeResourceForBackingStore(const QByteArray &resource, QBackingStore *backingStore) const
{
    NativeResourceForBackingStoreFunction func = handlerNativeResourceFunctionForBackingStore(resource);
    if (func)
        return func(backingStore);
    return nullptr;
}

static void dumpNativeWindowsRecursion(const XcbConnection *connection, xcb_window_t window,
                                       int level, QTextStream &str)
{
    if (level)
        str << QByteArray(2 * level, ' ');

    xcb_connection_t *conn = connection->xcb_connection();
    auto geomReply = Q_XCB_REPLY(xcb_get_geometry, conn, window);
    if (!geomReply)
        return;
    const QRect geom(geomReply->x, geomReply->y, geomReply->width, geomReply->height);
    if (!geom.isValid() || (geom.width() <= 3 && geom.height() <= 3))
        return; // Skip helper/dummy windows.
    str << "0x";
    const int oldFieldWidth = str.fieldWidth();
    const QChar oldPadChar =str.padChar();
    str.setFieldWidth(8);
    str.setPadChar(QLatin1Char('0'));
    str << Qt::hex << window;
    str.setFieldWidth(oldFieldWidth);
    str.setPadChar(oldPadChar);
    str << Qt::dec << " \""
        << geom.width() << 'x' << geom.height() << Qt::forcesign << geom.x() << geom.y()
        << Qt::noforcesign << '\n';

    auto reply = Q_XCB_REPLY(xcb_query_tree, conn, window);
    if (reply) {
        const int count = xcb_query_tree_children_length(reply.get());
        const xcb_window_t *children = xcb_query_tree_children(reply.get());
        for (int i = 0; i < count; ++i)
            dumpNativeWindowsRecursion(connection, children[i], level + 1, str);
    }
}

QString MPlatformNativeInterface::dumpConnectionNativeWindows(const XcbConnection *connection, WId root) const
{
    QString result;
    QTextStream str(&result);
    if (root) {
        dumpNativeWindowsRecursion(connection, xcb_window_t(root), 0, str);
    } else {
        const MPlatformScreen *screen = connection->primaryScreen();
        str << "Screen: \"" << screen->name() << "\"\n";
        dumpNativeWindowsRecursion(connection, screen->root(), 0, str);
        str << '\n';
    }
    return result;
}

QString MPlatformNativeInterface::dumpNativeWindows(WId root) const
{
    return dumpConnectionNativeWindows(MPlatformIntegration::instance()->defaultConnection(), root);
}
