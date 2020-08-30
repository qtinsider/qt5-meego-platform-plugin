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

#ifndef MPLATFORMNATIVEINTERFACE_H
#define MPLATFORMNATIVEINTERFACE_H

#include <qpa/qplatformnativeinterface.h>
#include <xcb/xcb.h>

#include <QtCore/QRect>

#include "mexport.h"
#include "xcbconnection.h"

class MPlatformScreen;

class M_EXPORT MPlatformNativeInterface : public QPlatformNativeInterface
{
    Q_OBJECT
public:
    enum ResourceType {
        Display,
        Connection,
        Screen,
        AppTime,
        AppUserTime,
        StartupId,
        GetTimestamp,
        RootWindow,
        GeneratePeekerId,
        RemovePeekerId,
        PeekEventQueue
    };

    MPlatformNativeInterface();

    void *nativeResourceForIntegration(const QByteArray &resource) override;
    void *nativeResourceForContext(const QByteArray &resourceString, QOpenGLContext *context) override;
    void *nativeResourceForScreen(const QByteArray &resource, QScreen *screen) override;
    void *nativeResourceForWindow(const QByteArray &resourceString, QWindow *window) override;
    void *nativeResourceForBackingStore(const QByteArray &resource, QBackingStore *backingStore) override;

    NativeResourceForIntegrationFunction nativeResourceFunctionForIntegration(const QByteArray &resource) override;
    NativeResourceForContextFunction nativeResourceFunctionForContext(const QByteArray &resource) override;
    NativeResourceForScreenFunction nativeResourceFunctionForScreen(const QByteArray &resource) override;
    NativeResourceForWindowFunction nativeResourceFunctionForWindow(const QByteArray &resource) override;
    NativeResourceForBackingStoreFunction nativeResourceFunctionForBackingStore(const QByteArray &resource) override;

    QFunctionPointer platformFunction(const QByteArray &function) const override;

    inline const QByteArray &nativeEventType() const { return m_nativeEventType; }

    void *displayForWindow(QWindow *window);
    void *connectionForWindow(QWindow *window);
    void *screenForWindow(QWindow *window);
    void *appTime(const MPlatformScreen *screen);
    void *appUserTime(const MPlatformScreen *screen);
    void *getTimestamp(const MPlatformScreen *screen);
    void *startupId();
    void *rootWindow();
    void *display();
    void *connection();
    static void setStartupId(const char *);
    static void setAppTime(QScreen *screen, xcb_timestamp_t time);
    static void setAppUserTime(QScreen *screen, xcb_timestamp_t time);

    static qint32 generatePeekerId();
    static bool removePeekerId(qint32 peekerId);
    static bool peekEventQueue(XcbEventQueue::PeekerCallback peeker, void *peekerData = nullptr,
                               XcbEventQueue::PeekOptions option = XcbEventQueue::PeekDefault,
                               qint32 peekerId = -1);

    Q_INVOKABLE QString dumpConnectionNativeWindows(const XcbConnection *connection, WId root) const;
    Q_INVOKABLE QString dumpNativeWindows(WId root = 0) const;

private:
    const QByteArray m_nativeEventType = QByteArrayLiteral("xcb_generic_event_t");

    static MPlatformScreen *qPlatformScreenForWindow(QWindow *window);

    NativeResourceForIntegrationFunction handlerNativeResourceFunctionForIntegration(const QByteArray &resource) const;
    NativeResourceForContextFunction handlerNativeResourceFunctionForContext(const QByteArray &resource) const;
    NativeResourceForScreenFunction handlerNativeResourceFunctionForScreen(const QByteArray &resource) const;
    NativeResourceForWindowFunction handlerNativeResourceFunctionForWindow(const QByteArray &resource) const;
    NativeResourceForBackingStoreFunction handlerNativeResourceFunctionForBackingStore(const QByteArray &resource) const;
    QFunctionPointer handlerPlatformFunction(const QByteArray &function) const;
    void *handlerNativeResourceForIntegration(const QByteArray &resource) const;
    void *handlerNativeResourceForContext(const QByteArray &resource, QOpenGLContext *context) const;
    void *handlerNativeResourceForScreen(const QByteArray &resource, QScreen *screen) const;
    void *handlerNativeResourceForWindow(const QByteArray &resource, QWindow *window) const;
    void *handlerNativeResourceForBackingStore(const QByteArray &resource, QBackingStore *backingStore) const;
};

#endif // MPLATFORMNATIVEINTERFACE_H
