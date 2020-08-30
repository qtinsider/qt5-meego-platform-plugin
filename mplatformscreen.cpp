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

#include "mplatformscreen.h"
#include "mplatformwindow.h"
#include "xcbimage.h"
#include "qnamespace.h"

#include <cstdio>

#include <QDebug>
#include <QtAlgorithms>

#include <qpa/qwindowsysteminterface.h>
#include <private/qmath_p.h>
#include <QtGui/private/qhighdpiscaling_p.h>

MPlatformScreen::MPlatformScreen(XcbConnection *connection, xcb_screen_t *screen)
    : XcbObject(connection)
    , m_screen(screen)
{
    auto rootAttribs = Q_XCB_REPLY_UNCHECKED(xcb_get_window_attributes, xcb_connection(),
                                             screen->root);
    const quint32 existingEventMask = !rootAttribs ? 0 : rootAttribs->your_event_mask;

    const quint32 mask = XCB_CW_EVENT_MASK;
    const quint32 values[] = {
        // XCB_CW_EVENT_MASK
        XCB_EVENT_MASK_ENTER_WINDOW
        | XCB_EVENT_MASK_LEAVE_WINDOW
        | XCB_EVENT_MASK_PROPERTY_CHANGE
        | existingEventMask // don't overwrite the event mask on the root window
    };

    updateProperties();

    xcb_change_window_attributes(xcb_connection(), screen->root, mask, values);

    xcb_depth_iterator_t depth_iterator =
        xcb_screen_allowed_depths_iterator(screen);

    while (depth_iterator.rem) {
        xcb_depth_t *depth = depth_iterator.data;
        xcb_visualtype_iterator_t visualtype_iterator =
            xcb_depth_visuals_iterator(depth);

        while (visualtype_iterator.rem) {
            xcb_visualtype_t *visualtype = visualtype_iterator.data;
            m_visuals.insert(visualtype->visual_id, *visualtype);
            m_visualDepths.insert(visualtype->visual_id, depth->depth);
            xcb_visualtype_next(&visualtype_iterator);
        }

        xcb_depth_next(&depth_iterator);
    }
}

MPlatformScreen::~MPlatformScreen()
{
}

QRect MPlatformScreen::geometry() const
{
    return QRect(QPoint(), QSize(screen()->width_in_pixels, screen()->height_in_pixels));
}

QRect MPlatformScreen::availableGeometry() const
{
    return QRect(QPoint(), QSize(screen()->width_in_pixels, screen()->height_in_pixels));
}

int MPlatformScreen::depth() const
{
    return m_screen->root_depth;
}

QImage::Format MPlatformScreen::format() const
{
    QImage::Format format;
    bool needsRgbSwap;
    qt_xcb_imageFormatForVisual(connection(), screen()->root_depth, visualForId(screen()->root_visual), &format, &needsRgbSwap);
    // We are ignoring needsRgbSwap here and just assumes the backing-store will handle it.
    if (format != QImage::Format_Invalid)
        return format;
    return QImage::Format_RGB32;
}

QSizeF MPlatformScreen::physicalSize() const
{
    return QSizeF(m_screen->width_in_millimeters, m_screen->height_in_millimeters);
}

QDpi MPlatformScreen::logicalDpi() const
{
    const QSize size = QSize(screen()->width_in_pixels, screen()->height_in_pixels);
    const QSize sizeMillimeters = QSize(screen()->width_in_millimeters, screen()->height_in_millimeters);

    return QDpi(Q_MM_PER_INCH * size.width() / sizeMillimeters.width(),
                Q_MM_PER_INCH * size.height() / sizeMillimeters.height());
}

static inline bool translate(xcb_connection_t *connection, xcb_window_t child, xcb_window_t parent,
                             int *x, int *y)
{
    auto translate_reply = Q_XCB_REPLY_UNCHECKED(xcb_translate_coordinates, connection, child, parent, *x, *y);
    if (!translate_reply)
        return false;
    *x = translate_reply->dst_x;
    *y = translate_reply->dst_y;
    return true;
}

QPixmap MPlatformScreen::grabWindow(WId window, int xIn, int yIn, int width, int height) const
{
    if (width == 0 || height == 0)
        return QPixmap();

    int x = xIn;
    int y = yIn;
    MPlatformScreen *screen = const_cast<MPlatformScreen *>(this);
    xcb_window_t root = screen->root();

    auto rootReply = Q_XCB_REPLY_UNCHECKED(xcb_get_geometry, xcb_connection(), root);
    if (!rootReply)
        return QPixmap();

    const quint8 rootDepth = rootReply->depth;

    QSize windowSize;
    quint8 effectiveDepth = 0;
    if (window) {
        auto windowReply = Q_XCB_REPLY_UNCHECKED(xcb_get_geometry, xcb_connection(), window);
        if (!windowReply)
            return QPixmap();
        windowSize = QSize(windowReply->width, windowReply->height);
        effectiveDepth = windowReply->depth;
        if (effectiveDepth == rootDepth) {
            // if the depth of the specified window and the root window are the
            // same, grab pixels from the root window (so that we get the any
            // overlapping windows and window manager frames)

            // map x and y to the root window
            if (!translate(xcb_connection(), window, root, &x, &y))
                return QPixmap();

            window = root;
        }
    } else {
        window = root;
        effectiveDepth = rootDepth;
        windowSize = geometry().size();
        x += geometry().x();
        y += geometry().y();
    }

    if (width < 0)
        width = windowSize.width() - xIn;
    if (height < 0)
        height = windowSize.height() - yIn;

    auto attributes_reply = Q_XCB_REPLY_UNCHECKED(xcb_get_window_attributes,
                                                  xcb_connection(),
                                                  window);

    if (!attributes_reply)
        return QPixmap();

    const xcb_visualtype_t *visual = screen->visualForId(attributes_reply->visual);

    xcb_pixmap_t pixmap = xcb_generate_id(xcb_connection());
    xcb_create_pixmap(xcb_connection(), effectiveDepth, pixmap, window, width, height);

    uint32_t gc_value_mask = XCB_GC_SUBWINDOW_MODE;
    uint32_t gc_value_list[] = {XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS};

    xcb_gcontext_t gc = xcb_generate_id(xcb_connection());
    xcb_create_gc(xcb_connection(), gc, pixmap, gc_value_mask, gc_value_list);

    xcb_copy_area(xcb_connection(), window, pixmap, gc, x, y, 0, 0, width, height);

    QPixmap result
        = qt_xcb_pixmapFromXPixmap(connection(), pixmap, width, height, effectiveDepth, visual);
    xcb_free_gc(xcb_connection(), gc);
    xcb_free_pixmap(xcb_connection(), pixmap);

    return result;
}

void MPlatformScreen::windowShown(MPlatformWindow *window)
{
    // Freedesktop.org Startup Notification
    if (!connection()->startupId().isEmpty() && window->window()->isTopLevel()) {
        sendStartupMessage(QByteArrayLiteral("remove: ID=") + connection()->startupId());
        connection()->clearStartupId();
    }
}

void MPlatformScreen::updateProperties()
{
    xcb_randr_output_t *outputs = nullptr;
    auto resources = Q_XCB_REPLY(xcb_randr_get_screen_resources, xcb_connection(), screen()->root);
    if (resources) {
        outputs = xcb_randr_get_screen_resources_outputs(resources.get());
    }
    auto output = Q_XCB_REPLY_UNCHECKED(xcb_randr_get_output_info,
                                        xcb_connection(), outputs[0], resources->config_timestamp);
    m_outputName = QString::fromUtf8((const char*)xcb_randr_get_output_info_name(output.get()),
                                     xcb_randr_get_output_info_name_length(output.get()));
}

QSurfaceFormat MPlatformScreen::surfaceFormatFor(const QSurfaceFormat &format) const
{
    const xcb_visualid_t xcb_visualid = connection()->hasDefaultVisualId() ? connection()->defaultVisualId()
                                                                           : screen()->root_visual;
    const xcb_visualtype_t *xcb_visualtype = visualForId(xcb_visualid);

    const int redSize = qPopulationCount(xcb_visualtype->red_mask);
    const int greenSize = qPopulationCount(xcb_visualtype->green_mask);
    const int blueSize = qPopulationCount(xcb_visualtype->blue_mask);

    QSurfaceFormat result = format;

    if (result.redBufferSize() < 0)
        result.setRedBufferSize(redSize);

    if (result.greenBufferSize() < 0)
        result.setGreenBufferSize(greenSize);

    if (result.blueBufferSize() < 0)
        result.setBlueBufferSize(blueSize);

    return result;
}

const xcb_visualtype_t *MPlatformScreen::visualForFormat(const QSurfaceFormat &format) const
{
    const xcb_visualtype_t *candidate = nullptr;

    for (const xcb_visualtype_t &xcb_visualtype : m_visuals) {

        const int redSize = qPopulationCount(xcb_visualtype.red_mask);
        const int greenSize = qPopulationCount(xcb_visualtype.green_mask);
        const int blueSize = qPopulationCount(xcb_visualtype.blue_mask);
        const int alphaSize = depthOfVisual(xcb_visualtype.visual_id) - redSize - greenSize - blueSize;

        if (format.redBufferSize() != -1 && redSize != format.redBufferSize())
            continue;

        if (format.greenBufferSize() != -1 && greenSize != format.greenBufferSize())
            continue;

        if (format.blueBufferSize() != -1 && blueSize != format.blueBufferSize())
            continue;

        if (format.alphaBufferSize() != -1 && alphaSize != format.alphaBufferSize())
            continue;

        // Try to find a RGB visual rather than e.g. BGR or GBR
        if (qCountTrailingZeroBits(xcb_visualtype.blue_mask) == 0)
            return &xcb_visualtype;

        // In case we do not find anything we like, just remember the first one
        // and hope for the best:
        if (!candidate)
            candidate = &xcb_visualtype;
    }

    return candidate;
}

const xcb_visualtype_t *MPlatformScreen::visualForId(xcb_visualid_t visualid) const
{
    QMap<xcb_visualid_t, xcb_visualtype_t>::const_iterator it = m_visuals.find(visualid);
    if (it == m_visuals.constEnd())
        return nullptr;
    return &*it;
}

quint8 MPlatformScreen::depthOfVisual(xcb_visualid_t visualid) const
{
    QMap<xcb_visualid_t, quint8>::const_iterator it = m_visualDepths.find(visualid);
    if (it == m_visualDepths.constEnd())
        return 0;
    return *it;
}

void MPlatformScreen::sendStartupMessage(const QByteArray &message) const
{
    xcb_window_t rootWindow = root();

    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 8;
    ev.type = connection()->atom(XcbAtom::_NET_STARTUP_INFO_BEGIN);
    ev.sequence = 0;
    ev.window = rootWindow;
    int sent = 0;
    int length = message.length() + 1; // include NUL byte
    const char *data = message.constData();
    do {
        if (sent == 20)
            ev.type = connection()->atom(XcbAtom::_NET_STARTUP_INFO);

        const int start = sent;
        const int numBytes = qMin(length - start, 20);
        memcpy(ev.data.data8, data + start, numBytes);
        xcb_send_event(connection()->xcb_connection(), false, rootWindow, XCB_EVENT_MASK_PROPERTY_CHANGE, (const char *) &ev);

        sent += numBytes;
    } while (sent < length);
}

static inline void formatRect(QDebug &debug, const QRect r)
{
    debug << r.width() << 'x' << r.height()
        << Qt::forcesign << r.x() << r.y() << Qt::noforcesign;
}

static inline void formatSizeF(QDebug &debug, const QSizeF s)
{
    debug << s.width() << 'x' << s.height() << "mm";
}

QDebug operator<<(QDebug debug, const MPlatformScreen *screen)
{
    const QDebugStateSaver saver(debug);
    debug.nospace();
    debug << "MScreen(" << (const void *)screen;
    if (screen) {
        debug << Qt::fixed << qSetRealNumberPrecision(1);
        debug << ", name=" << screen->name();
        debug << ", geometry=";
        formatRect(debug, screen->geometry());
        debug << ", availableGeometry=";
        formatRect(debug, screen->availableGeometry());
        debug << ", devicePixelRatio=" << screen->devicePixelRatio();
        debug << ", logicalDpi=" << screen->logicalDpi();
        debug << ", physicalSize=";
        formatSizeF(debug, screen->physicalSize());
        debug << "), orientation=" << screen->orientation();
        debug << ", depth=" << screen->depth();
        debug << ", root=" << Qt::hex << screen->root();
    }
    debug << ')';
    return debug;
}
