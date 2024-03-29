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

#include <QtGui/private/qguiapplication_p.h>
#include <QtCore/QDebug>
#include <QtCore/QCoreApplication>

#include "qxcbconnection.h"
#include "qxcbkeyboard.h"
#include "qxcbwindow.h"
#include "qxcbclipboard.h"
#include "qxcbwmsupport.h"
#include "qxcbnativeinterface.h"
#include "qxcbintegration.h"
#include "qxcbcursor.h"
#include "qxcbbackingstore.h"
#include "qxcbeventqueue.h"

#include <QAbstractEventDispatcher>
#include <QByteArray>
#include <QScopedPointer>

#include <stdio.h>
#include <errno.h>

#include <xcb/xfixes.h>
#define explicit dont_use_cxx_explicit
#include <xcb/xkb.h>
#undef explicit
#include <xcb/xinput.h>

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcQpaXInput, "qt.qpa.input")
Q_LOGGING_CATEGORY(lcQpaXInputDevices, "qt.qpa.input.devices")
Q_LOGGING_CATEGORY(lcQpaXInputEvents, "qt.qpa.input.events")
Q_LOGGING_CATEGORY(lcQpaScreen, "qt.qpa.screen")
Q_LOGGING_CATEGORY(lcQpaEvents, "qt.qpa.events")
Q_LOGGING_CATEGORY(lcQpaEventReader, "qt.qpa.events.reader")
Q_LOGGING_CATEGORY(lcQpaPeeker, "qt.qpa.peeker")
Q_LOGGING_CATEGORY(lcQpaKeyboard, "qt.qpa.xkeyboard")
Q_LOGGING_CATEGORY(lcQpaClipboard, "qt.qpa.clipboard")

QXcbConnection::QXcbConnection(QXcbNativeInterface *nativeInterface, bool canGrabServer, xcb_visualid_t defaultVisualId, const char *displayName)
    : QXcbBasicConnection(displayName)
    , m_canGrabServer(canGrabServer)
    , m_defaultVisualId(defaultVisualId)
    , m_nativeInterface(nativeInterface)
{
    if (!isConnected())
        return;

    m_eventQueue = new QXcbEventQueue(this);

    if (hasXRandr())
        xrandrSelectEvents();

    initializeScreens();

    if (hasXInput2()) {
        xi2SetupDevices();
    }

    m_wmSupport.reset(new QXcbWMSupport(this));
    m_keyboard = new QXcbKeyboard(this);
#ifndef QT_NO_CLIPBOARD
    m_clipboard = new QXcbClipboard(this);
#endif
    m_startupId = qgetenv("DESKTOP_STARTUP_ID");
    if (!m_startupId.isNull())
        qunsetenv("DESKTOP_STARTUP_ID");

    const int focusInDelay = 100;
    m_focusInTimer.setSingleShot(true);
    m_focusInTimer.setInterval(focusInDelay);
    m_focusInTimer.callOnTimeout([]() {
        // No FocusIn events for us, proceed with FocusOut normally.
        QWindowSystemInterface::handleWindowActivated(nullptr, Qt::ActiveWindowFocusReason);
    });

    sync();
}

QXcbConnection::~QXcbConnection()
{
#ifndef QT_NO_CLIPBOARD
    delete m_clipboard;
#endif
    if (m_eventQueue)
        delete m_eventQueue;

    // Delete screens in reverse order to avoid crash in case of multiple screens
    while (!m_screens.isEmpty())
        QWindowSystemInterface::handleScreenRemoved(m_screens.takeLast());

    while (!m_virtualDesktops.isEmpty())
        delete m_virtualDesktops.takeLast();

    delete m_keyboard;
}

QXcbScreen *QXcbConnection::primaryScreen() const
{
    if (!m_screens.isEmpty()) {
        Q_ASSERT(m_screens.first()->screenNumber() == primaryScreenNumber());
        return m_screens.first();
    }

    return nullptr;
}

void QXcbConnection::addWindowEventListener(xcb_window_t id, QXcbWindowEventListener *eventListener)
{
    m_mapper.insert(id, eventListener);
}

void QXcbConnection::removeWindowEventListener(xcb_window_t id)
{
    m_mapper.remove(id);
}

QXcbWindowEventListener *QXcbConnection::windowEventListenerFromId(xcb_window_t id)
{
    return m_mapper.value(id, nullptr);
}

QXcbWindow *QXcbConnection::platformWindowFromId(xcb_window_t id)
{
    QXcbWindowEventListener *listener = m_mapper.value(id, nullptr);
    if (listener)
        return listener->toWindow();
    return nullptr;
}

#define HANDLE_PLATFORM_WINDOW_EVENT(event_t, windowMember, handler) \
{ \
    auto e = reinterpret_cast<event_t *>(event); \
    if (QXcbWindowEventListener *eventListener = windowEventListenerFromId(e->windowMember))  { \
        if (eventListener->handleNativeEvent(event)) \
            return; \
        eventListener->handler(e); \
    } \
} \
break;

#define HANDLE_KEYBOARD_EVENT(event_t, handler) \
{ \
    auto e = reinterpret_cast<event_t *>(event); \
    if (QXcbWindowEventListener *eventListener = windowEventListenerFromId(e->event)) { \
        if (eventListener->handleNativeEvent(event)) \
            return; \
        m_keyboard->handler(e); \
    } \
} \
break;

void QXcbConnection::printXcbEvent(const QLoggingCategory &log, const char *message,
                                   xcb_generic_event_t *event) const
{
    quint8 response_type = event->response_type & ~0x80;
    quint16 sequence = event->sequence;

#define PRINT_AND_RETURN(name) { \
    qCDebug(log, "%s | %s(%d) | sequence: %d", message, name, response_type, sequence); \
    return; \
}
#define CASE_PRINT_AND_RETURN(name) case name : PRINT_AND_RETURN(#name);

    switch (response_type) {
    CASE_PRINT_AND_RETURN( XCB_KEY_PRESS );
    CASE_PRINT_AND_RETURN( XCB_KEY_RELEASE );
    CASE_PRINT_AND_RETURN( XCB_BUTTON_PRESS );
    CASE_PRINT_AND_RETURN( XCB_BUTTON_RELEASE );
    CASE_PRINT_AND_RETURN( XCB_MOTION_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_ENTER_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_LEAVE_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_FOCUS_IN );
    CASE_PRINT_AND_RETURN( XCB_FOCUS_OUT );
    CASE_PRINT_AND_RETURN( XCB_KEYMAP_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_EXPOSE );
    CASE_PRINT_AND_RETURN( XCB_GRAPHICS_EXPOSURE );
    CASE_PRINT_AND_RETURN( XCB_NO_EXPOSURE );
    CASE_PRINT_AND_RETURN( XCB_VISIBILITY_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_CREATE_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_DESTROY_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_UNMAP_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_MAP_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_MAP_REQUEST );
    CASE_PRINT_AND_RETURN( XCB_REPARENT_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_CONFIGURE_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_CONFIGURE_REQUEST );
    CASE_PRINT_AND_RETURN( XCB_GRAVITY_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_RESIZE_REQUEST );
    CASE_PRINT_AND_RETURN( XCB_CIRCULATE_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_CIRCULATE_REQUEST );
    CASE_PRINT_AND_RETURN( XCB_PROPERTY_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_SELECTION_CLEAR );
    CASE_PRINT_AND_RETURN( XCB_SELECTION_REQUEST );
    CASE_PRINT_AND_RETURN( XCB_SELECTION_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_COLORMAP_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_CLIENT_MESSAGE );
    CASE_PRINT_AND_RETURN( XCB_MAPPING_NOTIFY );
    CASE_PRINT_AND_RETURN( XCB_GE_GENERIC );
    }
    // XFixes
    if (isXFixesType(response_type, XCB_XFIXES_SELECTION_NOTIFY))
          PRINT_AND_RETURN("XCB_XFIXES_SELECTION_NOTIFY");

    // XRandR
    if (isXRandrType(response_type, XCB_RANDR_NOTIFY))
        PRINT_AND_RETURN("XCB_RANDR_NOTIFY");
    if (isXRandrType(response_type, XCB_RANDR_SCREEN_CHANGE_NOTIFY))
        PRINT_AND_RETURN("XCB_RANDR_SCREEN_CHANGE_NOTIFY");

    // XKB
    if (isXkbType(response_type))
        PRINT_AND_RETURN("XCB_XKB_* event");

    // UNKNOWN
    qCDebug(log, "%s | unknown(%d) | sequence: %d", message, response_type, sequence);

#undef PRINT_AND_RETURN
#undef CASE_PRINT_AND_RETURN
}

const char *xcb_errors[] =
{
    "Success",
    "BadRequest",
    "BadValue",
    "BadWindow",
    "BadPixmap",
    "BadAtom",
    "BadCursor",
    "BadFont",
    "BadMatch",
    "BadDrawable",
    "BadAccess",
    "BadAlloc",
    "BadColor",
    "BadGC",
    "BadIDChoice",
    "BadName",
    "BadLength",
    "BadImplementation",
    "Unknown"
};

const char *xcb_protocol_request_codes[] =
{
    "Null",
    "CreateWindow",
    "ChangeWindowAttributes",
    "GetWindowAttributes",
    "DestroyWindow",
    "DestroySubwindows",
    "ChangeSaveSet",
    "ReparentWindow",
    "MapWindow",
    "MapSubwindows",
    "UnmapWindow",
    "UnmapSubwindows",
    "ConfigureWindow",
    "CirculateWindow",
    "GetGeometry",
    "QueryTree",
    "InternAtom",
    "GetAtomName",
    "ChangeProperty",
    "DeleteProperty",
    "GetProperty",
    "ListProperties",
    "SetSelectionOwner",
    "GetSelectionOwner",
    "ConvertSelection",
    "SendEvent",
    "GrabPointer",
    "UngrabPointer",
    "GrabButton",
    "UngrabButton",
    "ChangeActivePointerGrab",
    "GrabKeyboard",
    "UngrabKeyboard",
    "GrabKey",
    "UngrabKey",
    "AllowEvents",
    "GrabServer",
    "UngrabServer",
    "QueryPointer",
    "GetMotionEvents",
    "TranslateCoords",
    "WarpPointer",
    "SetInputFocus",
    "GetInputFocus",
    "QueryKeymap",
    "OpenFont",
    "CloseFont",
    "QueryFont",
    "QueryTextExtents",
    "ListFonts",
    "ListFontsWithInfo",
    "SetFontPath",
    "GetFontPath",
    "CreatePixmap",
    "FreePixmap",
    "CreateGC",
    "ChangeGC",
    "CopyGC",
    "SetDashes",
    "SetClipRectangles",
    "FreeGC",
    "ClearArea",
    "CopyArea",
    "CopyPlane",
    "PolyPoint",
    "PolyLine",
    "PolySegment",
    "PolyRectangle",
    "PolyArc",
    "FillPoly",
    "PolyFillRectangle",
    "PolyFillArc",
    "PutImage",
    "GetImage",
    "PolyText8",
    "PolyText16",
    "ImageText8",
    "ImageText16",
    "CreateColormap",
    "FreeColormap",
    "CopyColormapAndFree",
    "InstallColormap",
    "UninstallColormap",
    "ListInstalledColormaps",
    "AllocColor",
    "AllocNamedColor",
    "AllocColorCells",
    "AllocColorPlanes",
    "FreeColors",
    "StoreColors",
    "StoreNamedColor",
    "QueryColors",
    "LookupColor",
    "CreateCursor",
    "CreateGlyphCursor",
    "FreeCursor",
    "RecolorCursor",
    "QueryBestSize",
    "QueryExtension",
    "ListExtensions",
    "ChangeKeyboardMapping",
    "GetKeyboardMapping",
    "ChangeKeyboardControl",
    "GetKeyboardControl",
    "Bell",
    "ChangePointerControl",
    "GetPointerControl",
    "SetScreenSaver",
    "GetScreenSaver",
    "ChangeHosts",
    "ListHosts",
    "SetAccessControl",
    "SetCloseDownMode",
    "KillClient",
    "RotateProperties",
    "ForceScreenSaver",
    "SetPointerMapping",
    "GetPointerMapping",
    "SetModifierMapping",
    "GetModifierMapping",
    "Unknown"
};

void QXcbConnection::handleXcbError(xcb_generic_error_t *error)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    qintptr result = 0;
#else
    long result = 0;
#endif
    QAbstractEventDispatcher* dispatcher = QAbstractEventDispatcher::instance();
    if (dispatcher && dispatcher->filterNativeEvent(m_nativeInterface->nativeEventType(), error, &result))
        return;

    printXcbError("QXcbConnection: XCB error", error);
}

void QXcbConnection::printXcbError(const char *message, xcb_generic_error_t *error)
{
    uint clamped_error_code = qMin<uint>(error->error_code, (sizeof(xcb_errors) / sizeof(xcb_errors[0])) - 1);
    uint clamped_major_code = qMin<uint>(error->major_code, (sizeof(xcb_protocol_request_codes) / sizeof(xcb_protocol_request_codes[0])) - 1);

    qCWarning(lcQpaXcb, "%s: %d (%s), sequence: %d, resource id: %d, major code: %d (%s), minor code: %d",
             message,
             int(error->error_code), xcb_errors[clamped_error_code],
             int(error->sequence), int(error->resource_id),
             int(error->major_code), xcb_protocol_request_codes[clamped_major_code],
             int(error->minor_code));
}

namespace {
    typedef union {
        /* All XKB events share these fields. */
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    } _xkb_event;
}

void QXcbConnection::handleXcbEvent(xcb_generic_event_t *event)
{
    if (Q_UNLIKELY(lcQpaEvents().isDebugEnabled()))
        printXcbEvent(lcQpaEvents(), "Event", event);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    qintptr result = 0; // Used only by MS Windows
#else
    long result = 0; // Used only by MS Windows
#endif
    if (QAbstractEventDispatcher *dispatcher = QAbstractEventDispatcher::instance()) {
        if (dispatcher->filterNativeEvent(m_nativeInterface->nativeEventType(), event, &result))
            return;
    }

    uint response_type = event->response_type & ~0x80;

    bool handled = true;
    switch (response_type) {
    case XCB_EXPOSE:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_expose_event_t, window, handleExposeEvent);
    case XCB_CONFIGURE_NOTIFY:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_configure_notify_event_t, event, handleConfigureNotifyEvent);
    case XCB_MAP_NOTIFY:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_map_notify_event_t, event, handleMapNotifyEvent);
    case XCB_UNMAP_NOTIFY:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_unmap_notify_event_t, event, handleUnmapNotifyEvent);
    case XCB_DESTROY_NOTIFY:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_destroy_notify_event_t, event, handleDestroyNotifyEvent);
    case XCB_CLIENT_MESSAGE: {
        auto clientMessage = reinterpret_cast<xcb_client_message_event_t *>(event);
        if (clientMessage->format != 32)
            return;
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_client_message_event_t, window, handleClientMessageEvent);
    }
    case XCB_FOCUS_IN:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_focus_in_event_t, event, handleFocusInEvent);
    case XCB_FOCUS_OUT:
        HANDLE_PLATFORM_WINDOW_EVENT(xcb_focus_out_event_t, event, handleFocusOutEvent);
    case XCB_KEY_PRESS:
    {
        auto keyPress = reinterpret_cast<xcb_key_press_event_t *>(event);
        m_keyboard->updateXKBStateFromCore(keyPress->state);
        setTime(keyPress->time);
        HANDLE_KEYBOARD_EVENT(xcb_key_press_event_t, handleKeyPressEvent);
    }
    case XCB_KEY_RELEASE:
        m_keyboard->updateXKBStateFromCore(reinterpret_cast<xcb_key_release_event_t *>(event)->state);
        HANDLE_KEYBOARD_EVENT(xcb_key_release_event_t, handleKeyReleaseEvent);
    case XCB_MAPPING_NOTIFY:
        m_keyboard->updateKeymap(reinterpret_cast<xcb_mapping_notify_event_t *>(event));
        break;
    case XCB_SELECTION_REQUEST:
    {
#if QT_CONFIG(clipboard)
        auto selectionRequest = reinterpret_cast<xcb_selection_request_event_t *>(event);
#endif
        {
#ifndef QT_NO_CLIPBOARD
            m_clipboard->handleSelectionRequest(selectionRequest);
#endif
        }
        break;
    }
    case XCB_SELECTION_CLEAR:
        setTime((reinterpret_cast<xcb_selection_clear_event_t *>(event))->time);
#ifndef QT_NO_CLIPBOARD
        m_clipboard->handleSelectionClearRequest(reinterpret_cast<xcb_selection_clear_event_t *>(event));
#endif
        break;
    case XCB_SELECTION_NOTIFY:
        setTime((reinterpret_cast<xcb_selection_notify_event_t *>(event))->time);
        break;
    case XCB_PROPERTY_NOTIFY:
    {
#ifndef QT_NO_CLIPBOARD
        if (m_clipboard->handlePropertyNotify(event))
            break;
#endif
        auto propertyNotify = reinterpret_cast<xcb_property_notify_event_t *>(event);
        if (propertyNotify->atom == atom(QXcbAtom::_NET_WORKAREA)) {
            QXcbVirtualDesktop *virtualDesktop = virtualDesktopForRootWindow(propertyNotify->window);
            if (virtualDesktop)
                virtualDesktop->updateWorkArea();
        } else {
            HANDLE_PLATFORM_WINDOW_EVENT(xcb_property_notify_event_t, window, handlePropertyNotifyEvent);
        }
        break;
    }
    case XCB_GE_GENERIC:
        // Here the windowEventListener is invoked from xi2HandleEvent()
        if (hasXInput2() && isXIEvent(event))
            xi2HandleEvent(reinterpret_cast<xcb_ge_event_t *>(event));
        break;
    default:
        handled = false; // event type not recognized
        break;
    }

    if (handled)
        return;

    handled = true;
    if (isXFixesType(response_type, XCB_XFIXES_SELECTION_NOTIFY)) {
        auto notify_event = reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(event);
        setTime(notify_event->timestamp);
#ifndef QT_NO_CLIPBOARD
        m_clipboard->handleXFixesSelectionRequest(notify_event);
#endif
        for (QXcbVirtualDesktop *virtualDesktop : qAsConst(m_virtualDesktops))
            virtualDesktop->handleXFixesSelectionNotify(notify_event);
    } else if (isXRandrType(response_type, XCB_RANDR_NOTIFY)) {
        updateScreens(reinterpret_cast<xcb_randr_notify_event_t *>(event));
    } else if (isXRandrType(response_type, XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
        auto change_event = reinterpret_cast<xcb_randr_screen_change_notify_event_t *>(event);
        if (auto virtualDesktop = virtualDesktopForRootWindow(change_event->root))
            virtualDesktop->handleScreenChange(change_event);
    } else if (isXkbType(response_type)) {
        auto xkb_event = reinterpret_cast<_xkb_event *>(event);
        if (xkb_event->any.deviceID == m_keyboard->coreDeviceId()) {
            switch (xkb_event->any.xkbType) {
                // XkbNewKkdNotify and XkbMapNotify together capture all sorts of keymap
                // updates (e.g. xmodmap, xkbcomp, setxkbmap), with minimal redundent recompilations.
                case XCB_XKB_STATE_NOTIFY:
                    m_keyboard->updateXKBState(&xkb_event->state_notify);
                    break;
                case XCB_XKB_MAP_NOTIFY:
                    m_keyboard->updateKeymap();
                    break;
                case XCB_XKB_NEW_KEYBOARD_NOTIFY: {
                    xcb_xkb_new_keyboard_notify_event_t *ev = &xkb_event->new_keyboard_notify;
                    if (ev->changed & XCB_XKB_NKN_DETAIL_KEYCODES)
                        m_keyboard->updateKeymap();
                    break;
                }
                default:
                    break;
            }
        }
    } else {
        handled = false; // event type still not recognized
    }

    if (handled)
        return;
}

void QXcbConnection::setFocusWindow(QWindow *w)
{
    m_focusWindow = w ? static_cast<QXcbWindow *>(w->handle()) : nullptr;
}
void QXcbConnection::setMouseGrabber(QXcbWindow *w)
{
    m_mouseGrabber = w;
}

void QXcbConnection::grabServer()
{
    if (m_canGrabServer)
        xcb_grab_server(xcb_connection());
}

void QXcbConnection::ungrabServer()
{
    if (m_canGrabServer)
        xcb_ungrab_server(xcb_connection());
}

xcb_timestamp_t QXcbConnection::getTimestamp()
{
    // send a dummy event to myself to get the timestamp from X server.
    xcb_window_t window = rootWindow();
    xcb_atom_t dummyAtom = atom(QXcbAtom::CLIP_TEMPORARY);
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_APPEND, window, dummyAtom,
                        XCB_ATOM_INTEGER, 32, 0, nullptr);

    connection()->flush();

    xcb_generic_event_t *event = nullptr;

    while (!event) {
        connection()->sync();
        event = eventQueue()->peek([window, dummyAtom](xcb_generic_event_t *event, int type) {
            if (type != XCB_PROPERTY_NOTIFY)
                return false;
            auto propertyNotify = reinterpret_cast<xcb_property_notify_event_t *>(event);
            return propertyNotify->window == window && propertyNotify->atom == dummyAtom;
        });
    }

    xcb_property_notify_event_t *pn = reinterpret_cast<xcb_property_notify_event_t *>(event);
    xcb_timestamp_t timestamp = pn->time;
    free(event);

    xcb_delete_property(xcb_connection(), window, dummyAtom);

    return timestamp;
}

xcb_window_t QXcbConnection::getSelectionOwner(xcb_atom_t atom) const
{
    return Q_XCB_REPLY(xcb_get_selection_owner, xcb_connection(), atom)->owner;
}

xcb_window_t QXcbConnection::getQtSelectionOwner()
{
    if (!m_qtSelectionOwner) {
        xcb_screen_t *xcbScreen = primaryVirtualDesktop()->screen();
        int16_t x = 0, y = 0;
        uint16_t w = 3, h = 3;
        m_qtSelectionOwner = xcb_generate_id(xcb_connection());
        xcb_create_window(xcb_connection(),
                          XCB_COPY_FROM_PARENT,               // depth -- same as root
                          m_qtSelectionOwner,                 // window id
                          xcbScreen->root,                    // parent window id
                          x, y, w, h,
                          0,                                  // border width
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,      // window class
                          xcbScreen->root_visual,             // visual
                          0,                                  // value mask
                          nullptr);                                 // value list

        QXcbWindow::setWindowTitle(connection(), m_qtSelectionOwner,
                                   QLatin1String("Qt Selection Owner for ") + QCoreApplication::applicationName());
    }
    return m_qtSelectionOwner;
}

xcb_window_t QXcbConnection::rootWindow()
{
    QXcbScreen *s = primaryScreen();
    return s ? s->root() : 0;
}

xcb_window_t QXcbConnection::clientLeader()
{
    if (m_clientLeader == 0) {
        m_clientLeader = xcb_generate_id(xcb_connection());
        QXcbScreen *screen = primaryScreen();
        xcb_create_window(xcb_connection(),
                          XCB_COPY_FROM_PARENT,
                          m_clientLeader,
                          screen->root(),
                          0, 0, 1, 1,
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          screen->screen()->root_visual,
                          0, nullptr);


        QXcbWindow::setWindowTitle(connection(), m_clientLeader,
                                   QGuiApplication::applicationDisplayName());

        xcb_change_property(xcb_connection(),
                            XCB_PROP_MODE_REPLACE,
                            m_clientLeader,
                            atom(QXcbAtom::WM_CLIENT_LEADER),
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            &m_clientLeader);

        // If we are session managed, inform the window manager about it
        QByteArray session = qGuiApp->sessionId().toLatin1();
        if (!session.isEmpty()) {
            xcb_change_property(xcb_connection(),
                                XCB_PROP_MODE_REPLACE,
                                m_clientLeader,
                                atom(QXcbAtom::SM_CLIENT_ID),
                                XCB_ATOM_STRING,
                                8,
                                session.length(),
                                session.constData());
        }
    }
    return m_clientLeader;
}

/*! \internal

    Compresses events of the same type to avoid swamping the event queue.
    If event compression is not desired there are several options what developers can do:

    1) Write responsive applications. We drop events that have been buffered in the event
       queue while waiting on unresponsive GUI thread.
    2) Use QAbstractNativeEventFilter to get all events from X connection. This is not optimal
       because it requires working with native event types.
    3) Or add public API to Qt for disabling event compression QTBUG-44964

*/
bool QXcbConnection::compressEvent(xcb_generic_event_t *event) const
{
    if (!QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents))
        return false;

    uint responseType = event->response_type & ~0x80;

    if (responseType == XCB_MOTION_NOTIFY) {
        // compress XCB_MOTION_NOTIFY notify events
        return m_eventQueue->peek(QXcbEventQueue::PeekRetainMatch,
                                  [](xcb_generic_event_t *, int type) {
            return type == XCB_MOTION_NOTIFY;
        });
    }

    // compress XI_* events
    if (responseType == XCB_GE_GENERIC) {
        if (!hasXInput2())
            return false;

        // compress XI_Motion
        if (isXIType(event, XCB_INPUT_MOTION)) {
            return m_eventQueue->peek(QXcbEventQueue::PeekRetainMatch,
                                      [this](xcb_generic_event_t *next, int) {
                return isXIType(next, XCB_INPUT_MOTION);
            });
        }

        return false;
    }

    if (responseType == XCB_CONFIGURE_NOTIFY) {
        // compress multiple configure notify events for the same window
        return m_eventQueue->peek(QXcbEventQueue::PeekRetainMatch,
                                  [event](xcb_generic_event_t *next, int type) {
            if (type != XCB_CONFIGURE_NOTIFY)
                return false;
            auto currentEvent = reinterpret_cast<xcb_configure_notify_event_t *>(event);
            auto nextEvent = reinterpret_cast<xcb_configure_notify_event_t *>(next);
            return currentEvent->event == nextEvent->event;
        });
    }

    return false;
}

bool QXcbConnection::isUserInputEvent(xcb_generic_event_t *event) const
{
    auto eventType = event->response_type & ~0x80;
    bool isInputEvent = isXIType(event, XCB_INPUT_BUTTON_PRESS) ||
                       isXIType(event, XCB_INPUT_BUTTON_RELEASE) ||
                       isXIType(event, XCB_INPUT_MOTION) ||
                       isXIType(event, XCB_INPUT_ENTER) ||
                       isXIType(event, XCB_INPUT_LEAVE);

    if (isInputEvent)
        return true;

    if (eventType == XCB_CLIENT_MESSAGE) {
        auto clientMessage = reinterpret_cast<const xcb_client_message_event_t *>(event);
        if (clientMessage->format == 32 && clientMessage->type == atom(QXcbAtom::WM_PROTOCOLS))
            if (clientMessage->data.data32[0] == atom(QXcbAtom::WM_DELETE_WINDOW))
                isInputEvent = true;
    }

    return isInputEvent;
}

void QXcbConnection::processXcbEvents(QEventLoop::ProcessEventsFlags flags)
{
    int connection_error = xcb_connection_has_error(xcb_connection());
    if (connection_error) {
        qWarning("The X11 connection broke (error %d). Did the X11 server die?", connection_error);
        exit(1);
    }

    m_eventQueue->flushBufferedEvents();

    while (xcb_generic_event_t *event = m_eventQueue->takeFirst(flags)) {
        QScopedPointer<xcb_generic_event_t, QScopedPointerPodDeleter> eventGuard(event);

        if (!(event->response_type & ~0x80)) {
            handleXcbError(reinterpret_cast<xcb_generic_error_t *>(event));
            continue;
        }

        if (compressEvent(event))
            continue;

        handleXcbEvent(event);

        // The lock-based solution used to free the lock inside this loop,
        // hence allowing for more events to arrive. ### Check if we want
        // this flush here after QTBUG-70095
        m_eventQueue->flushBufferedEvents();
    }

    xcb_flush(xcb_connection());
}

const xcb_format_t *QXcbConnection::formatForDepth(uint8_t depth) const
{
    xcb_format_iterator_t iterator =
        xcb_setup_pixmap_formats_iterator(setup());

    while (iterator.rem) {
        xcb_format_t *format = iterator.data;
        if (format->depth == depth)
            return format;
        xcb_format_next(&iterator);
    }

    qWarning() << "XCB failed to find an xcb_format_t for depth:" << depth;
    return nullptr;
}

void QXcbConnection::sync()
{
    // from xcb_aux_sync
    xcb_get_input_focus_cookie_t cookie = xcb_get_input_focus(xcb_connection());
    free(xcb_get_input_focus_reply(xcb_connection(), cookie, nullptr));
}

bool QXcbConnection::event(QEvent *e)
{
    if (e->type() == QEvent::User + 1) {
        QXcbSyncWindowRequest *ev = static_cast<QXcbSyncWindowRequest *>(e);
        QXcbWindow *w = ev->window();
        if (w) {
            w->updateSyncRequestCounter();
            ev->invalidate();
        }
        return true;
    }
    return QObject::event(e);
}

void QXcbSyncWindowRequest::invalidate()
{
    if (m_window) {
        m_window->clearSyncWindowRequest();
        m_window = nullptr;
    }
}

QXcbConnectionGrabber::QXcbConnectionGrabber(QXcbConnection *connection)
    :m_connection(connection)
{
    connection->grabServer();
}

QXcbConnectionGrabber::~QXcbConnectionGrabber()
{
    if (m_connection)
        m_connection->ungrabServer();
}

void QXcbConnectionGrabber::release()
{
    if (m_connection) {
        m_connection->ungrabServer();
        m_connection = nullptr;
    }
}

QT_END_NAMESPACE
