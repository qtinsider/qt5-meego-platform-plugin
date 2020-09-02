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

#include "mplatformwindow.h"

#include <QtDebug>
#include <QMetaEnum>
#include <QScreen>
#include <QtGui/QRegion>
#include <QtGui/private/qhighdpiscaling_p.h>

#include "mplatformintegration.h"
#include "xcbconnection.h"
#include "mplatformscreen.h"
#include "xcbimage.h"
#include "xcbwmsupport.h"
#include "xcbimage.h"
#include "mplatformnativeinterface.h"

#include <qpa/qplatformintegration.h>

#include <algorithm>

#include <xcb/xcb_icccm.h>
#include <xcb/xfixes.h>
#include <xcb/shape.h>
#include <xcb/xinput.h>

#include <private/qguiapplication_p.h>
#include <private/qwindow_p.h>

#include <qpa/qplatformbackingstore.h>
#include <qpa/qwindowsysteminterface.h>

#define register        /* C++17 deprecated register */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef register

Q_DECLARE_TYPEINFO(xcb_rectangle_t, Q_PRIMITIVE_TYPE);

#undef FocusIn

enum QX11EmbedFocusInDetail {
    XEMBED_FOCUS_CURRENT = 0,
    XEMBED_FOCUS_FIRST = 1,
    XEMBED_FOCUS_LAST = 2
};

enum QX11EmbedInfoFlags {
    XEMBED_MAPPED = (1 << 0),
};

enum QX11EmbedMessageType {
    XEMBED_EMBEDDED_NOTIFY = 0,
    XEMBED_WINDOW_ACTIVATE = 1,
    XEMBED_WINDOW_DEACTIVATE = 2,
    XEMBED_REQUEST_FOCUS = 3,
    XEMBED_FOCUS_IN = 4,
    XEMBED_FOCUS_OUT = 5,
    XEMBED_FOCUS_NEXT = 6,
    XEMBED_FOCUS_PREV = 7,
    XEMBED_MODALITY_ON = 10,
    XEMBED_MODALITY_OFF = 11,
    XEMBED_REGISTER_ACCELERATOR = 12,
    XEMBED_UNREGISTER_ACCELERATOR = 13,
    XEMBED_ACTIVATE_ACCELERATOR = 14
};

const quint32 XEMBED_VERSION = 0;

MPlatformWindow::MPlatformWindow(QWindow *window)
    : QPlatformWindow(window)
{
    setConnection(xcbScreen()->connection());
}

MPlatformWindow::~MPlatformWindow()
{
    destroy();
}

void MPlatformWindow::setGeometry(const QRect &rect)
{
    QPlatformWindow::setGeometry(rect);

    propagateSizeHints();

    MPlatformScreen *currentScreen = xcbScreen();
    MPlatformScreen *newScreen = parent() ? parentScreen() : static_cast<MPlatformScreen*>(screenForGeometry(rect));

    if (!newScreen)
        newScreen = xcbScreen();

    if (newScreen != currentScreen)
        QWindowSystemInterface::handleWindowScreenChanged(window(), newScreen->QPlatformScreen::screen());

    const quint32 mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    const qint32 values[] = {
        rect.x(),
        rect.y(),
        rect.width(),
        rect.height(),
     };
     xcb_configure_window(xcb_connection(), m_window, mask, reinterpret_cast<const quint32*>(values));
     if (window()->parent() && !window()->transientParent()) {
         // Wait for server reply for parented windows to ensure that a few window
         // moves will come as a one event. This is important when native widget is
         // moved a few times in X and Y directions causing native scroll. Widget
         // must get single event to not trigger unwanted widget position changes
         // and then expose events causing backingstore flushes with incorrect
         // offset causing image crruption.
         connection()->sync();
     }

    xcb_flush(xcb_connection());
}

void MPlatformWindow::setWindowState(Qt::WindowStates state)
{
    if (state == m_windowState)
        return;

    if ((m_windowState & Qt::WindowMinimized) && !(state & Qt::WindowMinimized)) {
        xcb_map_window(xcb_connection(), m_window);
    } else if (!(m_windowState & Qt::WindowMinimized) && (state & Qt::WindowMinimized)) {
        xcb_client_message_event_t event;

        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.sequence = 0;
        event.window = m_window;
        event.type = atom(XcbAtom::WM_CHANGE_STATE);
        event.data.data32[0] = XCB_ICCCM_WM_STATE_ICONIC;
        event.data.data32[1] = 0;
        event.data.data32[2] = 0;
        event.data.data32[3] = 0;
        event.data.data32[4] = 0;

        xcb_send_event(xcb_connection(), 0, xcbScreen()->root(),
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       (const char *)&event);
        m_minimized = true;
    }

    xcb_get_property_cookie_t cookie = xcb_icccm_get_wm_hints_unchecked(xcb_connection(), m_window);
    xcb_icccm_wm_hints_t hints;
    if (xcb_icccm_get_wm_hints_reply(xcb_connection(), cookie, &hints, nullptr)) {
        if (state & Qt::WindowMinimized)
            xcb_icccm_wm_hints_set_iconic(&hints);
        else
            xcb_icccm_wm_hints_set_normal(&hints);
        xcb_icccm_set_wm_hints(xcb_connection(), m_window, &hints);
    }

    connection()->sync();
    m_windowState = state;
}

void MPlatformWindow::setParent(const QPlatformWindow *parent)
{
    QPoint topLeft = geometry().topLeft();

    xcb_window_t xcb_parent_id;
    if (parent) {
        const MPlatformWindow *qMParent = static_cast<const MPlatformWindow *>(parent);
        xcb_parent_id = qMParent->xcb_window();
    } else {
        xcb_parent_id = xcbScreen()->root();
    }
    xcb_reparent_window(xcb_connection(), xcb_window(), xcb_parent_id, topLeft.x(), topLeft.y());
}

void MPlatformWindow::handleContentOrientationChange(Qt::ScreenOrientation orientation)
{
    int angle = 0;
    switch (orientation) {
    case Qt::PortraitOrientation: angle = 270; break;
    case Qt::LandscapeOrientation: angle = 0; break;
    case Qt::InvertedPortraitOrientation: angle = 90; break;
    case Qt::InvertedLandscapeOrientation: angle = 180; break;
    case Qt::PrimaryOrientation: break;
    }
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                        atom(XcbAtom::_MEEGOTOUCH_ORIENTATION_ANGLE), XCB_ATOM_CARDINAL, 32,
                        1, &angle);
}

void MPlatformWindow::setVisible(bool visible)
{
    if (visible)
        show();
    else
        hide();
}

void MPlatformWindow::setOpacity(qreal level)
{
    if (!m_window)
        return;

    quint32 value = qRound64(qBound(qreal(0), level, qreal(1)) * 0xffffffff);

    xcb_change_property(xcb_connection(),
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atom(XcbAtom::_NET_WM_WINDOW_OPACITY),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        (uchar *) &value);
}

bool MPlatformWindow::isExposed() const
{
    return m_mapped;
}

void MPlatformWindow::propagateSizeHints()
{
    // update WM_NORMAL_HINTS
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));

    const QRect rect = geometry();
    QWindowPrivate *win = qt_window_private(window());

    if (!win->positionAutomatic)
        xcb_icccm_size_hints_set_position(&hints, true, rect.x(), rect.y());

    xcb_icccm_set_wm_normal_hints(xcb_connection(), m_window, &hints);

    m_sizeHintsScaleFactor = QHighDpiScaling::scaleAndOrigin(screen()).factor;
}

void MPlatformWindow::raise()
{
    const quint32 mask = XCB_CONFIG_WINDOW_STACK_MODE;
    const quint32 values[] = { XCB_STACK_MODE_ABOVE };
    xcb_configure_window(xcb_connection(), m_window, mask, values);
}

void MPlatformWindow::lower()
{
    const quint32 mask = XCB_CONFIG_WINDOW_STACK_MODE;
    const quint32 values[] = { XCB_STACK_MODE_BELOW };
    xcb_configure_window(xcb_connection(), m_window, mask, values);
}

void MPlatformWindow::requestActivateWindow()
{
    if (!m_mapped) {
        m_deferredActivation = true;
        return;
    }
    m_deferredActivation = false;

    updateNetWmUserTime(connection()->time());
    QWindow *focusWindow = QGuiApplication::focusWindow();

    if (window()->isTopLevel()
        && !(window()->flags() & Qt::X11BypassWindowManagerHint)
        && (!focusWindow || !window()->isAncestorOf(focusWindow))
        && connection()->wmSupport()->isSupportedByWM(atom(XcbAtom::_NET_ACTIVE_WINDOW))) {
        xcb_client_message_event_t event;

        event.response_type = XCB_CLIENT_MESSAGE;
        event.format = 32;
        event.sequence = 0;
        event.window = m_window;
        event.type = atom(XcbAtom::_NET_ACTIVE_WINDOW);
        event.data.data32[0] = 1;
        event.data.data32[1] = connection()->time();
        event.data.data32[2] = focusWindow ? focusWindow->winId() : XCB_NONE;
        event.data.data32[3] = 0;
        event.data.data32[4] = 0;

        xcb_send_event(xcb_connection(), 0, xcbScreen()->root(),
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       (const char *) &event);
    } else {
        xcb_set_input_focus(xcb_connection(), XCB_INPUT_FOCUS_PARENT, m_window, connection()->time());
    }

    connection()->sync();
}

bool MPlatformWindow::setMouseGrabEnabled(bool grab)
{
    if (!grab && connection()->mouseGrabber() == this)
        connection()->setMouseGrabber(nullptr);

    if (grab && !connection()->canGrab())
        return false;

    if (connection()->hasXInput2()) {
        bool result = connection()->xi2SetMouseGrabEnabled(m_window, grab);
        if (grab && result)
            connection()->setMouseGrabber(this);
        return result;
    }

    if (!grab) {
        xcb_ungrab_pointer(xcb_connection(), XCB_TIME_CURRENT_TIME);
        return true;
    }

    auto reply = Q_XCB_REPLY(xcb_grab_pointer, xcb_connection(),
                             false, m_window,
                             (XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                              | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
                              | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_POINTER_MOTION),
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                             XCB_WINDOW_NONE, XCB_CURSOR_NONE,
                             XCB_TIME_CURRENT_TIME);
    bool result = reply && reply->status == XCB_GRAB_STATUS_SUCCESS;
    if (result)
        connection()->setMouseGrabber(this);
    return result;
}

WId MPlatformWindow::winId() const
{
    return m_window;
}

QSurfaceFormat MPlatformWindow::format() const
{
    return m_format;
}

bool MPlatformWindow::windowEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::FocusIn:
        if (!event->spontaneous()) {
            QFocusEvent *focusEvent = static_cast<QFocusEvent *>(event);
            switch (focusEvent->reason()) {
            case Qt::TabFocusReason:
            case Qt::BacktabFocusReason: {
                const MPlatformWindow *container = static_cast<const MPlatformWindow *>(parent());
                sendXEmbedMessage(container->xcb_window(),
                                  focusEvent->reason() == Qt::TabFocusReason ? XEMBED_FOCUS_NEXT
                                                                             : XEMBED_FOCUS_PREV);
                event->accept();
            } break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
    return QPlatformWindow::windowEvent(event);
}

void MPlatformWindow::setMask(const QRegion &region)
{
    if (!connection()->hasXShape())
        return;
    if (region.isEmpty()) {
        xcb_shape_mask(connection()->xcb_connection(), XCB_SHAPE_SO_SET,
                       XCB_SHAPE_SK_BOUNDING, xcb_window(), 0, 0, XCB_NONE);
    } else {
        const auto rects = qRegionToXcbRectangleList(region);
        xcb_shape_rectangles(connection()->xcb_connection(), XCB_SHAPE_SO_SET,
                             XCB_SHAPE_SK_BOUNDING, XCB_CLIP_ORDERING_UNSORTED,
                             xcb_window(), 0, 0, rects.size(), &rects[0]);
    }
}

enum : quint32 {
    baseEventMask
        = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
            | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE,

    defaultEventMask = baseEventMask
            | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
            | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
            | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
            | XCB_EVENT_MASK_POINTER_MOTION,

    transparentForInputEventMask = baseEventMask
            | XCB_EVENT_MASK_VISIBILITY_CHANGE | XCB_EVENT_MASK_RESIZE_REDIRECT
            | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
            | XCB_EVENT_MASK_COLOR_MAP_CHANGE | XCB_EVENT_MASK_OWNER_GRAB_BUTTON
};

void MPlatformWindow::create()
{
    destroy();

    m_windowState = Qt::WindowNoState;

    Qt::WindowType type = window()->type();

    MPlatformScreen *currentScreen = xcbScreen();
    MPlatformScreen *platformScreen = parent() ? parentScreen() : initialScreen();
    QRect rect = parent()
        ? QHighDpi::toNativeLocalPosition(window()->geometry(), platformScreen)
        : QHighDpi::toNativePixels(window()->geometry(), platformScreen);

    QPlatformWindow::setGeometry(rect);

    if (platformScreen != currentScreen)
        QWindowSystemInterface::handleWindowScreenChanged(window(), platformScreen->QPlatformScreen::screen());

    if (rect.width() > 0 || rect.height() > 0) {
        rect.setWidth(rect.width());
        rect.setHeight(rect.height());
    }

    xcb_window_t xcb_parent_id = platformScreen->root();
    if (parent()) {
        xcb_parent_id = static_cast<MPlatformWindow *>(parent())->xcb_window();

        QSurfaceFormat parentFormat = parent()->window()->requestedFormat();
        if (window()->surfaceType() != QSurface::OpenGLSurface && parentFormat.hasAlpha()) {
            window()->setFormat(parentFormat);
        }
    }

    resolveFormat(platformScreen->surfaceFormatFor(window()->requestedFormat()));

    const xcb_visualtype_t *visual = nullptr;

    if (connection()->hasDefaultVisualId()) {
        visual = platformScreen->visualForId(connection()->defaultVisualId());
        if (!visual)
            qWarning() << "Failed to use requested visual id.";
    }

    if (!visual)
        visual = createVisual();

    if (!visual) {
        qWarning() << "Falling back to using screens root_visual.";
        visual = platformScreen->visualForId(platformScreen->screen()->root_visual);
    }

    Q_ASSERT(visual);

    m_visualId = visual->visual_id;
    m_depth = platformScreen->depthOfVisual(m_visualId);
    setImageFormatForVisual(visual);

    quint32 mask = XCB_CW_BACK_PIXMAP
                 | XCB_CW_BORDER_PIXEL
                 | XCB_CW_BIT_GRAVITY
                 | XCB_CW_OVERRIDE_REDIRECT
                 | XCB_CW_SAVE_UNDER
                 | XCB_CW_EVENT_MASK;

    if (window()->supportsOpenGL() || m_format.hasAlpha()) {
        m_cmap = xcb_generate_id(xcb_connection());
        xcb_create_colormap(xcb_connection(),
                            XCB_COLORMAP_ALLOC_NONE,
                            m_cmap,
                            xcb_parent_id,
                            m_visualId);

        mask |= XCB_CW_COLORMAP;
    }

    quint32 values[] = {
        XCB_BACK_PIXMAP_NONE,
        platformScreen->screen()->black_pixel,
        XCB_GRAVITY_NORTH_WEST,
        type == Qt::Popup || type == Qt::ToolTip || (window()->flags() & Qt::BypassWindowManagerHint),
        type == Qt::Popup || type == Qt::Tool || type == Qt::SplashScreen || type == Qt::ToolTip || type == Qt::Drawer,
        defaultEventMask,
        m_cmap
    };

    m_window = xcb_generate_id(xcb_connection());
    xcb_create_window(xcb_connection(),
                      m_depth,
                      m_window,                        // window id
                      xcb_parent_id,                   // parent window id
                      rect.x(),
                      rect.y(),
                      rect.width(),
                      rect.height(),
                      0,                               // border width
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,   // window class
                      m_visualId,                      // visual
                      mask,
                      values);

    connection()->addWindowEventListener(m_window, this);

    propagateSizeHints();

    xcb_atom_t properties[5];
    int propertyCount = 0;
    properties[propertyCount++] = atom(XcbAtom::WM_DELETE_WINDOW);
    properties[propertyCount++] = atom(XcbAtom::WM_TAKE_FOCUS);
    properties[propertyCount++] = atom(XcbAtom::_NET_WM_PING);
    properties[propertyCount++] = atom(XcbAtom::_NET_WM_SYNC_REQUEST);

    xcb_change_property(xcb_connection(),
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atom(XcbAtom::WM_PROTOCOLS),
                        XCB_ATOM_ATOM,
                        32,
                        propertyCount,
                        properties);
    m_syncValue.hi = 0;
    m_syncValue.lo = 0;

    const QByteArray wmClass = MPlatformIntegration::instance()->wmClass();
    if (!wmClass.isEmpty()) {
        xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE,
                            m_window, atom(XcbAtom::WM_CLASS),
                            XCB_ATOM_STRING, 8, wmClass.size(), wmClass.constData());
    }

    m_syncCounter = xcb_generate_id(xcb_connection());
    xcb_sync_create_counter(xcb_connection(), m_syncCounter, m_syncValue);
    xcb_change_property(xcb_connection(),
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atom(XcbAtom::_NET_WM_SYNC_REQUEST_COUNTER),
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &m_syncCounter);

    // set the PID to let the WM kill the application if unresponsive
    quint32 pid = getpid();
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                        atom(XcbAtom::_NET_WM_PID), XCB_ATOM_CARDINAL, 32,
                        1, &pid);

    const QByteArray clientMachine = QSysInfo::machineHostName().toLocal8Bit();
    if (!clientMachine.isEmpty()) {
        xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                            atom(XcbAtom::WM_CLIENT_MACHINE), XCB_ATOM_STRING, 8,
                            clientMachine.size(), clientMachine.constData());
    }

    // Create WM_HINTS property on the window, so we can xcb_icccm_get_wm_hints*()
    // from various setter functions for adjusting the hints.
    xcb_icccm_wm_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    hints.flags = XCB_ICCCM_WM_HINT_WINDOW_GROUP;
    hints.window_group = connection()->clientLeader();
    xcb_icccm_set_wm_hints(xcb_connection(), m_window, &hints);

    xcb_window_t leader = connection()->clientLeader();
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                        atom(XcbAtom::WM_CLIENT_LEADER), XCB_ATOM_WINDOW, 32,
                        1, &leader);

    /* Add XEMBED info; this operation doesn't initiate the embedding. */
    quint32 data[] = { XEMBED_VERSION, XEMBED_MAPPED };
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                        atom(XcbAtom::_XEMBED_INFO),
                        atom(XcbAtom::_XEMBED_INFO),
                        32, 2, (void *) data);

    connection()->xi2SelectDeviceEvents(m_window);

    setWindowState(window()->windowStates());

    // force sync to read outstanding requests - see QTBUG-29106
    XSync(static_cast<Display*>(platformScreen->connection()->xlib_display()), false);

    const qreal opacity = qt_window_private(window())->opacity;
    if (!qFuzzyCompare(opacity, qreal(1.0)))
        setOpacity(opacity);

    setMask(QHighDpi::toNativeLocalRegion(window()->mask(), window()));
}

MPlatformScreen *MPlatformWindow::parentScreen()
{
    return parent() ? static_cast<MPlatformWindow*>(parent())->parentScreen() : xcbScreen();
}

//QPlatformWindow::screenForGeometry version that uses deviceIndependentGeometry
MPlatformScreen *MPlatformWindow::initialScreen() const
{
    QWindowPrivate *windowPrivate = qt_window_private(window());
    QScreen *screen = windowPrivate->screenForGeometry(window()->geometry());
    return static_cast<MPlatformScreen*>(screen->handle());
}

// Returns \c true if we should set WM_TRANSIENT_FOR on \a w
static inline bool isTransient(const QWindow *w)
{
    return w->type() == Qt::Dialog
           || w->type() == Qt::Sheet
           || w->type() == Qt::Tool
           || w->type() == Qt::SplashScreen
           || w->type() == Qt::ToolTip
           || w->type() == Qt::Drawer
           || w->type() == Qt::Popup;
}

void MPlatformWindow::setImageFormatForVisual(const xcb_visualtype_t *visual)
{
    if (qt_xcb_imageFormatForVisual(connection(), m_depth, visual, &m_imageFormat, &m_imageRgbSwap))
        return;

    switch (m_depth) {
    case 32:
    case 24:
        qWarning("Using RGB32 fallback, if this works your X11 server is reporting a bad screen format.");
        m_imageFormat = QImage::Format_RGB32;
        break;
    case 16:
        qWarning("Using RGB16 fallback, if this works your X11 server is reporting a bad screen format.");
        m_imageFormat = QImage::Format_RGB16;
    default:
        break;
    }
}

void MPlatformWindow::destroy()
{
    if (connection()->focusWindow() == this)
        doFocusOut();
    if (connection()->mouseGrabber() == this)
        connection()->setMouseGrabber(nullptr);

    if (m_syncCounter)
        xcb_sync_destroy_counter(xcb_connection(), m_syncCounter);
    if (m_window) {
        if (m_netWmUserTimeWindow) {
            xcb_delete_property(xcb_connection(), m_window, atom(XcbAtom::_NET_WM_USER_TIME_WINDOW));
            // Some window managers, like metacity, do XSelectInput on the _NET_WM_USER_TIME_WINDOW window,
            // without trapping BadWindow (which crashes when the user time window is destroyed).
            connection()->sync();
            xcb_destroy_window(xcb_connection(), m_netWmUserTimeWindow);
            m_netWmUserTimeWindow = XCB_NONE;
        }
        connection()->removeWindowEventListener(m_window);
        xcb_destroy_window(xcb_connection(), m_window);
        m_window = 0;
    }
    if (m_cmap) {
        xcb_free_colormap(xcb_connection(), m_cmap);
    }
    m_mapped = false;

    if (m_pendingSyncRequest)
        m_pendingSyncRequest->invalidate();
}

void MPlatformWindow::show()
{
    if (window()->isTopLevel()) {

        // update WM_NORMAL_HINTS
        propagateSizeHints();

        // update WM_TRANSIENT_FOR
        xcb_window_t transientXcbParent = 0;
        if (isTransient(window())) {
            const QWindow *tp = window()->transientParent();
            if (tp && tp->handle())
                transientXcbParent = static_cast<const MPlatformWindow *>(tp->handle())->winId();
            // Default to client leader if there is no transient parent, else modal dialogs can
            // be hidden by their parents.
            if (!transientXcbParent)
                transientXcbParent = connection()->clientLeader();
            if (transientXcbParent) { // ICCCM 4.1.2.6
                xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                                    XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32,
                                    1, &transientXcbParent);
            }
        }
        if (!transientXcbParent)
            xcb_delete_property(xcb_connection(), m_window, XCB_ATOM_WM_TRANSIENT_FOR);

        // update _NET_WM_STATE
        setNetWmStateOnUnmappedWindow();
    }

    // QWidget-attribute Qt::WA_ShowWithoutActivating.
    const auto showWithoutActivating = window()->property("_q_showWithoutActivating");
    if (showWithoutActivating.isValid() && showWithoutActivating.toBool())
        updateNetWmUserTime(0);
    else if (connection()->time() != XCB_TIME_CURRENT_TIME)
        updateNetWmUserTime(connection()->time());

    xcb_map_window(xcb_connection(), m_window);

    if (QGuiApplication::modalWindow() == window())
        requestActivateWindow();

    xcbScreen()->windowShown(this);

    connection()->sync();
}

void MPlatformWindow::hide()
{
    xcb_unmap_window(xcb_connection(), m_window);

    // send synthetic UnmapNotify event according to icccm 4.1.4
    q_padded_xcb_event<xcb_unmap_notify_event_t> event = {};
    event.response_type = XCB_UNMAP_NOTIFY;
    event.event = xcbScreen()->root();
    event.window = m_window;
    event.from_configure = false;
    xcb_send_event(xcb_connection(), false, xcbScreen()->root(),
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char *)&event);

    xcb_flush(xcb_connection());

    if (connection()->mouseGrabber() == this)
        connection()->setMouseGrabber(nullptr);

    m_mapped = false;
}

bool MPlatformWindow::relayFocusToModalWindow() const
{
    QWindow *w = static_cast<QWindowPrivate *>(QObjectPrivate::get(window()))->eventReceiver();
    // get top-level window
    while (w && w->parent())
        w = w->parent();

    QWindow *modalWindow = nullptr;
    const bool blocked = QGuiApplicationPrivate::instance()->isWindowBlocked(w, &modalWindow);
    if (blocked && modalWindow != w) {
        modalWindow->requestActivate();
        connection()->flush();
        return true;
    }

    return false;
}

void MPlatformWindow::doFocusIn()
{
    if (relayFocusToModalWindow())
        return;
    QWindow *w = static_cast<QWindowPrivate *>(QObjectPrivate::get(window()))->eventReceiver();
    connection()->setFocusWindow(w);
    QWindowSystemInterface::handleWindowActivated(w, Qt::ActiveWindowFocusReason);
}

void MPlatformWindow::doFocusOut()
{
    connection()->setFocusWindow(nullptr);
    relayFocusToModalWindow();
    // Do not set the active window to nullptr if there is a FocusIn coming.
    connection()->focusInTimer().start();
}

void MPlatformWindow::setNetWmStateOnUnmappedWindow()
{
    if (Q_UNLIKELY(m_mapped))
        qCWarning(lcQpaXcb()) << "internal error: " << Q_FUNC_INFO << "called on mapped window";

    // According to EWMH:
    //    "The Window Manager should remove _NET_WM_STATE whenever a window is withdrawn".
    // Which means that we don't have to read this property before changing it on a withdrawn
    // window. But there are situations where users want to adjust this property as well
    // (e4cea305ed2ba3c9f580bf9d16c59a1048af0e8a), so instead of overwriting the property
    // we first read it and then merge our hints with the existing values, allowing a user
    // to set custom hints.

    QVector<xcb_atom_t> atoms;
    auto reply = Q_XCB_REPLY_UNCHECKED(xcb_get_property, xcb_connection(),
                                       0, m_window, atom(XcbAtom::_NET_WM_STATE),
                                       XCB_ATOM_ATOM, 0, 1024);
    if (reply && reply->format == 32 && reply->type == XCB_ATOM_ATOM && reply->value_len > 0) {
        const xcb_atom_t *data = static_cast<const xcb_atom_t *>(xcb_get_property_value(reply.get()));
        atoms.resize(reply->value_len);
        memcpy((void *) &atoms.first(), (void *) data, reply->value_len * sizeof(xcb_atom_t));
    }

    if (!atoms.contains(atom(XcbAtom::_NET_WM_STATE_FULLSCREEN)))
        atoms.push_back(atom(XcbAtom::_NET_WM_STATE_FULLSCREEN));

    if (atoms.isEmpty()) {
        xcb_delete_property(xcb_connection(), m_window, atom(XcbAtom::_NET_WM_STATE));
    } else {
        xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                            atom(XcbAtom::_NET_WM_STATE), XCB_ATOM_ATOM, 32,
                            atoms.count(), atoms.constData());
    }
    xcb_flush(xcb_connection());

}

void MPlatformWindow::updateNetWmUserTime(xcb_timestamp_t timestamp)
{
    xcb_window_t wid = m_window;
    // If timestamp == 0, then it means that the window should not be
    // initially activated. Don't update global user time for this
    // special case.
    if (timestamp != 0)
        connection()->setNetWmUserTime(timestamp);

    const bool isSupportedByWM = connection()->wmSupport()->isSupportedByWM(atom(XcbAtom::_NET_WM_USER_TIME_WINDOW));
    if (m_netWmUserTimeWindow || isSupportedByWM) {
        if (!m_netWmUserTimeWindow) {
            m_netWmUserTimeWindow = xcb_generate_id(xcb_connection());
            xcb_create_window(xcb_connection(),
                              XCB_COPY_FROM_PARENT,            // depth -- same as root
                              m_netWmUserTimeWindow,           // window id
                              m_window,                        // parent window id
                              -1, -1, 1, 1,
                              0,                               // border width
                              XCB_WINDOW_CLASS_INPUT_OUTPUT,   // window class
                              m_visualId,                      // visual
                              0,                               // value mask
                              nullptr);                        // value list
            wid = m_netWmUserTimeWindow;
            xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, m_window, atom(XcbAtom::_NET_WM_USER_TIME_WINDOW),
                                XCB_ATOM_WINDOW, 32, 1, &m_netWmUserTimeWindow);
            xcb_delete_property(xcb_connection(), m_window, atom(XcbAtom::_NET_WM_USER_TIME));

        } else if (!isSupportedByWM) {
            // WM no longer supports it, then we should remove the
            // _NET_WM_USER_TIME_WINDOW atom.
            xcb_delete_property(xcb_connection(), m_window, atom(XcbAtom::_NET_WM_USER_TIME_WINDOW));
            xcb_destroy_window(xcb_connection(), m_netWmUserTimeWindow);
            m_netWmUserTimeWindow = XCB_NONE;
        } else {
            wid = m_netWmUserTimeWindow;
        }
    }
    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, wid, atom(XcbAtom::_NET_WM_USER_TIME),
                        XCB_ATOM_CARDINAL, 32, 1, &timestamp);
}

bool MPlatformWindow::handleNativeEvent(xcb_generic_event_t *event)
{
    auto eventType = connection()->nativeInterface()->nativeEventType();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    qintptr result = 0; // Used only by MS Windows
#else
    long result = 0; // Used only by MS Windows
#endif
    return QWindowSystemInterface::handleNativeEvent(window(), eventType, event, &result);
}

void MPlatformWindow::handleExposeEvent(const xcb_expose_event_t *event)
{
    QRect rect(event->x, event->y, event->width, event->height);
    m_exposeRegion |= rect;

    bool pending = true;

    connection()->eventQueue()->peek(XcbEventQueue::PeekRemoveMatchContinue,
                                     [this, &pending](xcb_generic_event_t *event, int type) {
        if (type != XCB_EXPOSE)
            return false;
        auto expose = reinterpret_cast<xcb_expose_event_t *>(event);
        if (expose->window != m_window)
            return false;
        if (expose->count == 0)
            pending = false;
        m_exposeRegion |= QRect(expose->x, expose->y, expose->width, expose->height);
        free(expose);
        return true;
    });

    // if count is non-zero there are more expose events pending
    if (event->count == 0 || !pending) {
        QWindowSystemInterface::handleExposeEvent(window(), m_exposeRegion);
        m_exposeRegion = QRegion();
    }
}

void MPlatformWindow::handleClientMessageEvent(const xcb_client_message_event_t *event)
{
    if (event->format != 32)
        return;

    if (event->type == atom(XcbAtom::WM_PROTOCOLS)) {
        xcb_atom_t protocolAtom = event->data.data32[0];
        if (protocolAtom == atom(XcbAtom::WM_DELETE_WINDOW)) {
            QWindowSystemInterface::handleCloseEvent(window());
        } else if (protocolAtom == atom(XcbAtom::WM_TAKE_FOCUS)) {
            connection()->setTime(event->data.data32[1]);
            relayFocusToModalWindow();
            return;
        } else if (protocolAtom == atom(XcbAtom::_NET_WM_PING)) {
            if (event->window == xcbScreen()->root())
                return;

            xcb_client_message_event_t reply = *event;

            reply.response_type = XCB_CLIENT_MESSAGE;
            reply.window = xcbScreen()->root();

            xcb_send_event(xcb_connection(), 0, xcbScreen()->root(),
                           XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                           (const char *)&reply);
            xcb_flush(xcb_connection());
        } else if (protocolAtom == atom(XcbAtom::_NET_WM_SYNC_REQUEST)) {
            connection()->setTime(event->data.data32[1]);
            m_syncValue.lo = event->data.data32[2];
            m_syncValue.hi = event->data.data32[3];
            m_syncState = SyncReceived;
        } else {
            qCWarning(lcQpaXcb, "Unhandled WM_PROTOCOLS (%s)",
                      connection()->atomName(protocolAtom).constData());
        }
    } else if (event->type == atom(XcbAtom::_XEMBED)) {
        handleXEmbedMessage(event);
    } else if (event->type == atom(XcbAtom::_NET_ACTIVE_WINDOW)) {
        doFocusIn();
    } else if (event->type == atom(XcbAtom::_NET_WM_STATE)
               || event->type == atom(XcbAtom::WM_CHANGE_STATE)) {
        // Ignore _NET_WM_STATE, and other messages.
    } else if (event->type == atom(XcbAtom::_MEEGOTOUCH_MINIMIZE_ANIMATION)) {
        // silence the _MEEGO messages for now
    } else {
        qCWarning(lcQpaXcb) << "Unhandled client message: " << connection()->atomName(event->type);
    }
}

void MPlatformWindow::handleConfigureNotifyEvent(const xcb_configure_notify_event_t *event)
{
    bool fromSendEvent = (event->response_type & 0x80);
    QPoint pos(event->x, event->y);
    if (!parent() && !fromSendEvent) {
        // Do not trust the position, query it instead.
        auto reply = Q_XCB_REPLY(xcb_translate_coordinates, xcb_connection(),
                                 xcb_window(), xcbScreen()->root(), 0, 0);
        if (reply) {
            pos.setX(reply->dst_x);
            pos.setY(reply->dst_y);
        }
    }

    const QRect actualGeometry = QRect(pos, QSize(event->width, event->height));
    QPlatformScreen *newScreen = parent() ? parent()->screen() : screenForGeometry(actualGeometry);
    if (!newScreen)
        return;

    QWindowSystemInterface::handleGeometryChange(window(), actualGeometry);

    // QPlatformScreen::screen() is updated asynchronously, so we can't compare it
    // with the newScreen. Just send the WindowScreenChanged event and QGuiApplication
    // will make the comparison later.
    QWindowSystemInterface::handleWindowScreenChanged(window(), newScreen->screen());

    if (!qFuzzyCompare(QHighDpiScaling::scaleAndOrigin(newScreen).factor, m_sizeHintsScaleFactor))
        propagateSizeHints();

    // Send the synthetic expose event on resize only when the window is shrinked,
    // because the "XCB_GRAVITY_NORTH_WEST" flag doesn't send it automatically.
    if (!m_oldWindowSize.isEmpty()) {
        QWindowSystemInterface::handleExposeEvent(window(), QRegion(0, 0, actualGeometry.width(), actualGeometry.height()));
    }
    m_oldWindowSize = actualGeometry.size();

    if (m_syncState == SyncReceived)
        m_syncState = SyncAndConfigureReceived;
}

void MPlatformWindow::handleMapNotifyEvent(const xcb_map_notify_event_t *event)
{
    if (event->window == m_window) {
        m_mapped = true;
        if (m_deferredActivation)
            requestActivateWindow();

        QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(), geometry().size()));
    }
}

void MPlatformWindow::handleUnmapNotifyEvent(const xcb_unmap_notify_event_t *event)
{
    if (event->window == m_window) {
        m_mapped = false;
        QWindowSystemInterface::handleExposeEvent(window(), QRegion());
    }
}

void MPlatformWindow::handleEnterNotifyEvent(int event_x, int event_y, int root_x, int root_y, xcb_timestamp_t timestamp)
{
    connection()->setTime(timestamp);

    const QPoint global = QPoint(root_x, root_y);

    const QPoint local(event_x, event_y);
    QWindowSystemInterface::handleEnterEvent(window(), local, global);
}

void MPlatformWindow::handleLeaveNotifyEvent(int root_x, int root_y, xcb_timestamp_t timestamp)
{
    connection()->setTime(timestamp);

    // check if enter event is buffered
    auto event = connection()->eventQueue()->peek([](xcb_generic_event_t *event, int type) {
        if (type != XCB_ENTER_NOTIFY)
            return false;
        auto enter = reinterpret_cast<xcb_enter_notify_event_t *>(event);
    });
    auto enter = reinterpret_cast<xcb_enter_notify_event_t *>(event);
    MPlatformWindow *enterWindow = enter ? connection()->platformWindowFromId(enter->event) : nullptr;

    if (enterWindow) {
        QPoint local(enter->event_x, enter->event_y);
        QPoint global = QPoint(root_x, root_y);
        QWindowSystemInterface::handleEnterLeaveEvent(enterWindow->window(), window(), local, global);
    } else {
        QWindowSystemInterface::handleLeaveEvent(window());
    }

    free(enter);
}

static inline int fixed1616ToInt(xcb_input_fp1616_t val)
{
    return int(qreal(val) / 0x10000);
}

void MPlatformWindow::handleXIEnterLeave(xcb_ge_event_t *event)
{
    auto *ev = reinterpret_cast<xcb_input_enter_event_t *>(event);

    // Compare the window with current mouse grabber to prevent deliver events to any other windows.
    // If leave event occurs and the window is under mouse - allow to deliver the leave event.
    MPlatformWindow *mouseGrabber = connection()->mouseGrabber();
    if (mouseGrabber && mouseGrabber != this
            && (ev->event_type != XCB_INPUT_LEAVE || QGuiApplicationPrivate::currentMouseWindow != window())) {
        return;
    }

    const int root_x = fixed1616ToInt(ev->root_x);
    const int root_y = fixed1616ToInt(ev->root_y);

    switch (ev->event_type) {
    case XCB_INPUT_ENTER: {
        const int event_x = fixed1616ToInt(ev->event_x);
        const int event_y = fixed1616ToInt(ev->event_y);
        qCDebug(lcQpaXInputEvents, "XI2 mouse enter %d,%d, mode %d, detail %d, time %d",
                event_x, event_y, ev->mode, ev->detail, ev->time);
        handleEnterNotifyEvent(event_x, event_y, root_x, root_y, ev->time);
        break;
    }
    case XCB_INPUT_LEAVE:
        qCDebug(lcQpaXInputEvents, "XI2 mouse leave, mode %d, detail %d, time %d",
                ev->mode, ev->detail, ev->time);
        handleLeaveNotifyEvent(root_x, root_y, ev->time);
        break;
    }
}

MPlatformWindow *MPlatformWindow::toWindow() { return this; }

void MPlatformWindow::handlePropertyNotifyEvent(const xcb_property_notify_event_t *event)
{
    connection()->setTime(event->time);

    const bool propertyDeleted = event->state == XCB_PROPERTY_DELETE;

    if (event->atom == atom(XcbAtom::_NET_WM_STATE) || event->atom == atom(XcbAtom::WM_STATE)) {
        if (propertyDeleted)
            return;

        Qt::WindowStates newState = Qt::WindowNoState;

        if (event->atom == atom(XcbAtom::WM_STATE)) { // WM_STATE: Quick check for 'Minimize'.
            auto reply = Q_XCB_REPLY(xcb_get_property, xcb_connection(),
                                     0, m_window, atom(XcbAtom::WM_STATE),
                                     XCB_ATOM_ANY, 0, 1024);
            if (reply && reply->format == 32 && reply->type == atom(XcbAtom::WM_STATE)) {
                const quint32 *data = (const quint32 *)xcb_get_property_value(reply.get());
                if (reply->length != 0)
                    m_minimized = (data[0] == XCB_ICCCM_WM_STATE_ICONIC
                                   || (data[0] == XCB_ICCCM_WM_STATE_WITHDRAWN && m_minimized));
            }
        }

        if (m_minimized)
            newState = Qt::WindowMinimized;
        else
            newState |= Qt::WindowFullScreen;

        // Send Window state, compress events in case other flags (modality, etc) are changed.
        if (m_lastWindowStateEvent != newState) {
            QWindowSystemInterface::handleWindowStateChanged(window(), newState);
            m_lastWindowStateEvent = newState;
            m_windowState = newState;
            if ((m_windowState & Qt::WindowMinimized) && connection()->mouseGrabber() == this)
                connection()->setMouseGrabber(nullptr);
        }
        return;
    }
}

void MPlatformWindow::handleFocusInEvent(const xcb_focus_in_event_t *event)
{
    // Ignore focus events that are being sent only because the pointer is over
    // our window, even if the input focus is in a different window.
    if (event->detail == XCB_NOTIFY_DETAIL_POINTER)
        return;

    connection()->focusInTimer().stop();
    doFocusIn();
}


void MPlatformWindow::handleFocusOutEvent(const xcb_focus_out_event_t *event)
{
    // Ignore focus events that are being sent only because the pointer is over
    // our window, even if the input focus is in a different window.
    if (event->detail == XCB_NOTIFY_DETAIL_POINTER)
        return;
    doFocusOut();
}

void MPlatformWindow::updateSyncRequestCounter()
{
    if (m_syncState != SyncAndConfigureReceived) {
        // window manager does not expect a sync event yet.
        return;
    }
    if ((m_syncValue.lo != 0 || m_syncValue.hi != 0)) {
        xcb_sync_set_counter(xcb_connection(), m_syncCounter, m_syncValue);
        xcb_flush(xcb_connection());

        m_syncValue.lo = 0;
        m_syncValue.hi = 0;
        m_syncState = NoSyncNeeded;
    }
}

const xcb_visualtype_t *MPlatformWindow::createVisual()
{
    return xcbScreen() ? xcbScreen()->visualForFormat(m_format)
                       : nullptr;
}

// Sends an XEmbed message.
void MPlatformWindow::sendXEmbedMessage(xcb_window_t window, quint32 message,
                                   quint32 detail, quint32 data1, quint32 data2)
{
    xcb_client_message_event_t event;

    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = window;
    event.type = atom(XcbAtom::_XEMBED);
    event.data.data32[0] = connection()->time();
    event.data.data32[1] = message;
    event.data.data32[2] = detail;
    event.data.data32[3] = data1;
    event.data.data32[4] = data2;
    xcb_send_event(xcb_connection(), false, window, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
}

static bool activeWindowChangeQueued(const QWindow *window)
{
    /* Check from window system event queue if the next queued activation
     * targets a window other than @window.
     */
    QWindowSystemInterfacePrivate::ActivatedWindowEvent *systemEvent =
        static_cast<QWindowSystemInterfacePrivate::ActivatedWindowEvent *>
        (QWindowSystemInterfacePrivate::peekWindowSystemEvent(QWindowSystemInterfacePrivate::ActivatedWindow));
    return systemEvent && systemEvent->activated != window;
}

void MPlatformWindow::handleXEmbedMessage(const xcb_client_message_event_t *event)
{
    connection()->setTime(event->data.data32[0]);
    switch (event->data.data32[1]) {
    case XEMBED_WINDOW_ACTIVATE:
    case XEMBED_WINDOW_DEACTIVATE:
        break;
    case XEMBED_EMBEDDED_NOTIFY:
        xcb_map_window(xcb_connection(), m_window);
        xcbScreen()->windowShown(this);
        break;
    case XEMBED_FOCUS_IN:
        connection()->focusInTimer().stop();
        Qt::FocusReason reason;
        switch (event->data.data32[2]) {
        case XEMBED_FOCUS_FIRST:
            reason = Qt::TabFocusReason;
            break;
        case XEMBED_FOCUS_LAST:
            reason = Qt::BacktabFocusReason;
            break;
        case XEMBED_FOCUS_CURRENT:
        default:
            reason = Qt::OtherFocusReason;
            break;
        }
        connection()->setFocusWindow(window());
        QWindowSystemInterface::handleWindowActivated(window(), reason);
        break;
    case XEMBED_FOCUS_OUT:
        if (window() == QGuiApplication::focusWindow()
            && !activeWindowChangeQueued(window())) {
            connection()->setFocusWindow(nullptr);
            QWindowSystemInterface::handleWindowActivated(nullptr);
        }
        break;
    }
}

static inline xcb_rectangle_t qRectToXCBRectangle(const QRect &r)
{
    xcb_rectangle_t result;
    result.x = qMax(SHRT_MIN, r.x());
    result.y = qMax(SHRT_MIN, r.y());
    result.width = qMin((int)USHRT_MAX, r.width());
    result.height = qMin((int)USHRT_MAX, r.height());
    return result;
}

QVector<xcb_rectangle_t> qRegionToXcbRectangleList(const QRegion &region)
{
    QVector<xcb_rectangle_t> rects;
    rects.reserve(region.rectCount());
    for (const QRect &r : region)
        rects.push_back(qRectToXCBRectangle(r));
    return rects;
}

uint MPlatformWindow::visualId() const
{
    return m_visualId;
}

bool MPlatformWindow::needsSync() const
{
    return m_syncState == SyncAndConfigureReceived;
}

void MPlatformWindow::postSyncWindowRequest()
{
    if (!m_pendingSyncRequest) {
        MSyncWindowRequest *e = new MSyncWindowRequest(this);
        m_pendingSyncRequest = e;
        QCoreApplication::postEvent(xcbScreen()->connection(), e);
    }
}

MPlatformScreen *MPlatformWindow::xcbScreen() const
{
    return static_cast<MPlatformScreen *>(screen());
}
