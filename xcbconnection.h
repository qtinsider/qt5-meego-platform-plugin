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

#ifndef XCBCONNECTION_H
#define XCBCONNECTION_H

#include <xcb/xcb.h>
#include <xcb/randr.h>

#include <QtCore/QTimer>
#include <QtGui/private/qtguiglobal_p.h>
#include "mexport.h"
#include <QHash>
#include <QList>
#include <QVector>
#include <qpa/qwindowsysteminterface.h>
#include <QtCore/QLoggingCategory>
#include <QtCore/private/qglobal_p.h>

#include "xcbeventqueue.h"
#include "xcbconnection_basic.h"

Q_DECLARE_LOGGING_CATEGORY(lcQpaXInput)
Q_DECLARE_LOGGING_CATEGORY(lcQpaXInputDevices)
Q_DECLARE_LOGGING_CATEGORY(lcQpaXInputEvents)
Q_DECLARE_LOGGING_CATEGORY(lcQpaScreen)
Q_DECLARE_LOGGING_CATEGORY(lcQpaEvents)
Q_DECLARE_LOGGING_CATEGORY(lcQpaPeeker)
Q_DECLARE_LOGGING_CATEGORY(lcQpaClipboard)
Q_DECLARE_LOGGING_CATEGORY(lcQpaEventReader)

class MPlatformScreen;
class MPlatformWindow;
class MPlatformClipboard;
class XcbWMSupport;
class MPlatformNativeInterface;

class MWindowEventListener
{
public:
    virtual ~MWindowEventListener() {}
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
    virtual MPlatformWindow *toWindow() { return nullptr; }
};

using WindowMapper = QHash<xcb_window_t, MWindowEventListener *>;

class MSyncWindowRequest : public QEvent
{
public:
    MSyncWindowRequest(MPlatformWindow *w) : QEvent(QEvent::Type(QEvent::User + 1)), m_window(w) { }

    MPlatformWindow *window() const { return m_window; }
    void invalidate();

private:
    MPlatformWindow *m_window;
};

class M_EXPORT XcbConnection : public XcbBasicConnection
{
    Q_OBJECT
public:
    XcbConnection(MPlatformNativeInterface *nativeInterface, bool canGrabServer, xcb_visualid_t defaultVisualId, const char *displayName = nullptr);
    ~XcbConnection();

    XcbConnection *connection() const { return const_cast<XcbConnection *>(this); }
    XcbEventQueue *eventQueue() const { return m_eventQueue; }

    MPlatformScreen *primaryScreen() const;

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
    MPlatformClipboard *clipboard() const { return m_clipboard; }

    XcbWMSupport *wmSupport() const { return m_wmSupport.data(); }
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

    void addWindowEventListener(xcb_window_t id, MWindowEventListener *eventListener);
    void removeWindowEventListener(xcb_window_t id);
    MWindowEventListener *windowEventListenerFromId(xcb_window_t id);
    MPlatformWindow *platformWindowFromId(xcb_window_t id);

    inline xcb_timestamp_t time() const { return m_time; }
    inline void setTime(xcb_timestamp_t t) { if (timeGreaterThan(t, m_time)) m_time = t; }

    inline xcb_timestamp_t netWmUserTime() const { return m_netWmUserTime; }
    inline void setNetWmUserTime(xcb_timestamp_t t) { if (timeGreaterThan(t, m_netWmUserTime)) m_netWmUserTime = t; }

    xcb_timestamp_t getTimestamp();
    xcb_window_t getSelectionOwner(xcb_atom_t atom) const;
    xcb_window_t getQtSelectionOwner();

    MPlatformWindow *focusWindow() const { return m_focusWindow; }
    void setFocusWindow(QWindow *);
    MPlatformWindow *mouseGrabber() const { return m_mouseGrabber; }
    void setMouseGrabber(MPlatformWindow *);

    QByteArray startupId() const { return m_startupId; }
    void setStartupId(const QByteArray &nextId) { m_startupId = nextId; }
    void clearStartupId() { m_startupId.clear(); }

    void grabServer();
    void ungrabServer();

    MPlatformNativeInterface *nativeInterface() const { return m_nativeInterface; }

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
    void xi2ProcessTouch(void *xiDevEvent, MPlatformWindow *platformWindow);

    static bool xi2GetValuatorValueIfSet(const void *event, int valuatorNum, double *value);

    QTouchDevice *m_touchDevices = nullptr;
    int maxTouchPoints = 1;

    const bool m_canGrabServer;
    const xcb_visualid_t m_defaultVisualId;

    MPlatformScreen *m_screen;

    xcb_timestamp_t m_time = XCB_CURRENT_TIME;
    xcb_timestamp_t m_netWmUserTime = XCB_CURRENT_TIME;

    MPlatformClipboard *m_clipboard = nullptr;

    QScopedPointer<XcbWMSupport> m_wmSupport;
    MPlatformNativeInterface *m_nativeInterface = nullptr;

    XcbEventQueue *m_eventQueue = nullptr;

    WindowMapper m_mapper;

    MPlatformWindow *m_focusWindow = nullptr;
    MPlatformWindow *m_mouseGrabber = nullptr;

    xcb_window_t m_clientLeader = 0;
    QByteArray m_startupId;
    bool m_xiGrab = false;
    QList<int> m_xiMasterPointerIds;

    xcb_window_t m_qtSelectionOwner = 0;

    friend class XcbEventQueue;

    QTimer m_focusInTimer;

};

class XcbConnectionGrabber
{
public:
    XcbConnectionGrabber(XcbConnection *connection);
    ~XcbConnectionGrabber();
    void release();
private:
    XcbConnection *m_connection;
};

// The xcb_send_event() requires all events to have 32 bytes. It calls memcpy() on the
// passed in event. If the passed in event is less than 32 bytes, memcpy() reaches into
// unrelated memory.
template <typename T>
struct alignas(32) q_padded_xcb_event : T { };

#endif
