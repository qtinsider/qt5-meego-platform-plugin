/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QXCBCONNECTION_H
#define QXCBCONNECTION_H

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <QtCore/QTimer>
#include <QtGui/private/qtguiglobal_p.h>
#include "qxcbexport.h"
#include <QHash>
#include <QList>
#include <QVector>
#include <qpa/qwindowsysteminterface.h>
#include <QtCore/QLoggingCategory>
#include <QtCore/private/qglobal_p.h>

#include "qxcbeventqueue.h"
#include "qxcbconnection_basic.h"

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(lcQpaXInput)
Q_DECLARE_LOGGING_CATEGORY(lcQpaXInputDevices)
Q_DECLARE_LOGGING_CATEGORY(lcQpaXInputEvents)
Q_DECLARE_LOGGING_CATEGORY(lcQpaScreen)
Q_DECLARE_LOGGING_CATEGORY(lcQpaEvents)
Q_DECLARE_LOGGING_CATEGORY(lcQpaPeeker)
Q_DECLARE_LOGGING_CATEGORY(lcQpaKeyboard)
Q_DECLARE_LOGGING_CATEGORY(lcQpaClipboard)
Q_DECLARE_LOGGING_CATEGORY(lcQpaEventReader)

class QXcbVirtualDesktop;
class QXcbScreen;
class QXcbWindow;
class QXcbKeyboard;
class QXcbClipboard;
class QXcbWMSupport;
class QXcbNativeInterface;

class QXcbWindowEventListener
{
public:
    virtual ~QXcbWindowEventListener() {}
    virtual bool handleNativeEvent(xcb_generic_event_t *) { return false; }

    virtual void handleExposeEvent(const xcb_expose_event_t *) {}
    virtual void handleClientMessageEvent(const xcb_client_message_event_t *) {}
    virtual void handleConfigureNotifyEvent(const xcb_configure_notify_event_t *) {}
    virtual void handleMapNotifyEvent(const xcb_map_notify_event_t *) {}
    virtual void handleUnmapNotifyEvent(const xcb_unmap_notify_event_t *) {}
    virtual void handleDestroyNotifyEvent(const xcb_destroy_notify_event_t *) {}
    virtual void handleFocusInEvent(const xcb_focus_in_event_t *) {}
    virtual void handleFocusOutEvent(const xcb_focus_out_event_t *) {}
    virtual void handlePropertyNotifyEvent(const xcb_property_notify_event_t *) {}
    virtual void handleXIEnterLeave(xcb_ge_event_t *) {}
    virtual QXcbWindow *toWindow() { return nullptr; }
};

using WindowMapper = QHash<xcb_window_t, QXcbWindowEventListener *>;

class QXcbSyncWindowRequest : public QEvent
{
public:
    QXcbSyncWindowRequest(QXcbWindow *w) : QEvent(QEvent::Type(QEvent::User + 1)), m_window(w) { }

    QXcbWindow *window() const { return m_window; }
    void invalidate();

private:
    QXcbWindow *m_window;
};

class Q_XCB_EXPORT QXcbConnection : public QXcbBasicConnection
{
    Q_OBJECT
public:
    QXcbConnection(QXcbNativeInterface *nativeInterface, bool canGrabServer, xcb_visualid_t defaultVisualId, const char *displayName = nullptr);
    ~QXcbConnection();

    QXcbConnection *connection() const { return const_cast<QXcbConnection *>(this); }
    QXcbEventQueue *eventQueue() const { return m_eventQueue; }

    const QList<QXcbVirtualDesktop *> &virtualDesktops() const { return m_virtualDesktops; }
    const QList<QXcbScreen *> &screens() const { return m_screens; }
    QXcbVirtualDesktop *primaryVirtualDesktop() const {
        return m_virtualDesktops.value(primaryScreenNumber());
    }
    QXcbScreen *primaryScreen() const;

    const xcb_format_t *formatForDepth(uint8_t depth) const;

    bool imageNeedsEndianSwap() const
    {
        if (!hasShm())
            return false; // The non-Shm path does its own swapping
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
        return setup()->image_byte_order != XCB_IMAGE_ORDER_MSB_FIRST;
#else
        return setup()->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST;
#endif
    }

    QXcbKeyboard *keyboard() const { return m_keyboard; }

#ifndef QT_NO_CLIPBOARD
    QXcbClipboard *clipboard() const { return m_clipboard; }
#endif

    QXcbWMSupport *wmSupport() const { return m_wmSupport.data(); }
    xcb_window_t rootWindow();
    xcb_window_t clientLeader();

    bool hasDefaultVisualId() const { return m_defaultVisualId != UINT_MAX; }
    xcb_visualid_t defaultVisualId() const { return m_defaultVisualId; }

    void sync();

    void handleXcbError(xcb_generic_error_t *error);
    void printXcbError(const char *message, xcb_generic_error_t *error);
    void handleXcbEvent(xcb_generic_event_t *event);
    void printXcbEvent(const QLoggingCategory &log, const char *message,
                       xcb_generic_event_t *event) const;

    void addWindowEventListener(xcb_window_t id, QXcbWindowEventListener *eventListener);
    void removeWindowEventListener(xcb_window_t id);
    QXcbWindowEventListener *windowEventListenerFromId(xcb_window_t id);
    QXcbWindow *platformWindowFromId(xcb_window_t id);

    inline xcb_timestamp_t time() const { return m_time; }
    inline void setTime(xcb_timestamp_t t) { if (timeGreaterThan(t, m_time)) m_time = t; }

    inline xcb_timestamp_t netWmUserTime() const { return m_netWmUserTime; }
    inline void setNetWmUserTime(xcb_timestamp_t t) { if (timeGreaterThan(t, m_netWmUserTime)) m_netWmUserTime = t; }

    xcb_timestamp_t getTimestamp();
    xcb_window_t getSelectionOwner(xcb_atom_t atom) const;
    xcb_window_t getQtSelectionOwner();

    QXcbWindow *focusWindow() const { return m_focusWindow; }
    void setFocusWindow(QWindow *);
    QXcbWindow *mouseGrabber() const { return m_mouseGrabber; }
    void setMouseGrabber(QXcbWindow *);

    QByteArray startupId() const { return m_startupId; }
    void setStartupId(const QByteArray &nextId) { m_startupId = nextId; }
    void clearStartupId() { m_startupId.clear(); }

    void grabServer();
    void ungrabServer();

    QXcbNativeInterface *nativeInterface() const { return m_nativeInterface; }

    bool isUserInputEvent(xcb_generic_event_t *event) const;

    void xi2SelectDeviceEvents(xcb_window_t window);
    bool xi2SetMouseGrabEnabled(xcb_window_t w, bool grab);

    bool canGrab() const { return m_canGrabServer; }

    void flush() { xcb_flush(xcb_connection()); }
    void processXcbEvents(QEventLoop::ProcessEventsFlags flags);

    QTimer &focusInTimer() { return m_focusInTimer; }

protected:
    bool event(QEvent *e) override;

private:
    void xrandrSelectEvents();
    QXcbScreen* findScreenForCrtc(xcb_window_t rootWindow, xcb_randr_crtc_t crtc) const;
    QXcbScreen* findScreenForOutput(xcb_window_t rootWindow, xcb_randr_output_t output) const;
    QXcbVirtualDesktop* virtualDesktopForRootWindow(xcb_window_t rootWindow) const;
    void updateScreens(const xcb_randr_notify_event_t *event);
    bool checkOutputIsPrimary(xcb_window_t rootWindow, xcb_randr_output_t output);
    void updateScreen(QXcbScreen *screen, const xcb_randr_output_change_t &outputChange);
    QXcbScreen *createScreen(QXcbVirtualDesktop *virtualDesktop,
                             const xcb_randr_output_change_t &outputChange,
                             xcb_randr_get_output_info_reply_t *outputInfo);
    void destroyScreen(QXcbScreen *screen);
    void initializeScreens();
    bool compressEvent(xcb_generic_event_t *event) const;
    inline bool timeGreaterThan(xcb_timestamp_t a, xcb_timestamp_t b) const
    { return static_cast<int32_t>(a - b) > 0 || b == XCB_CURRENT_TIME; }

    void xi2SetupDevices();
    struct ValuatorClassInfo {
        double min = 0.0;
        double max = 0.0;
        int number = -1;
        xcb_atom_t label = XCB_ATOM_NONE;
    };
    QList<ValuatorClassInfo> m_valuatorInfo;
    QList<QWindowSystemInterface::TouchPoint> m_touchPoints;

    void populateTouchDevices(void *info);
    void xi2HandleEvent(xcb_ge_event_t *event);
    void xi2ProcessTouch(void *xiDevEvent, QXcbWindow *platformWindow);

    static bool xi2GetValuatorValueIfSet(const void *event, int valuatorNum, double *value);

    QTouchDevice *m_touchDevices = nullptr;
    int maxTouchPoints = 1;

    const bool m_canGrabServer;
    const xcb_visualid_t m_defaultVisualId;

    QList<QXcbVirtualDesktop *> m_virtualDesktops;
    QList<QXcbScreen *> m_screens;

    xcb_timestamp_t m_time = XCB_CURRENT_TIME;
    xcb_timestamp_t m_netWmUserTime = XCB_CURRENT_TIME;

    QXcbKeyboard *m_keyboard = nullptr;
#ifndef QT_NO_CLIPBOARD
    QXcbClipboard *m_clipboard = nullptr;
#endif
    QScopedPointer<QXcbWMSupport> m_wmSupport;
    QXcbNativeInterface *m_nativeInterface = nullptr;

    QXcbEventQueue *m_eventQueue = nullptr;

    WindowMapper m_mapper;

    QXcbWindow *m_focusWindow = nullptr;
    QXcbWindow *m_mouseGrabber = nullptr;

    xcb_window_t m_clientLeader = 0;
    QByteArray m_startupId;
    bool m_xiGrab = false;
    QList<int> m_xiMasterPointerIds;

    xcb_window_t m_qtSelectionOwner = 0;

    friend class QXcbEventQueue;

    QTimer m_focusInTimer;

};

class QXcbConnectionGrabber
{
public:
    QXcbConnectionGrabber(QXcbConnection *connection);
    ~QXcbConnectionGrabber();
    void release();
private:
    QXcbConnection *m_connection;
};

// The xcb_send_event() requires all events to have 32 bytes. It calls memcpy() on the
// passed in event. If the passed in event is less than 32 bytes, memcpy() reaches into
// unrelated memory.
template <typename T>
struct alignas(32) q_padded_xcb_event : T { };

QT_END_NAMESPACE

#endif
