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

#ifndef MPLATFORMWINDOW_H
#define MPLATFORMWINDOW_H

#include <qpa/qplatformwindow.h>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QImage>

#include <xcb/xcb.h>
#include <xcb/sync.h>

#include "xcbobject.h"

class MPlatformScreen;
class MSyncWindowRequest;

class M_EXPORT MPlatformWindow : public XcbObject, public MWindowEventListener, public QPlatformWindow
{
public:
    MPlatformWindow(QWindow *window);
    ~MPlatformWindow();

    void setGeometry(const QRect &rect) override;

    void setWindowState(Qt::WindowStates state) override;
    void setParent(const QPlatformWindow *window) override;
    void handleContentOrientationChange(Qt::ScreenOrientation orientation) override;
    void setVisible(bool visible) override;
    void setOpacity(qreal level) override;

    bool isExposed() const override;
    void propagateSizeHints() override;

    void raise() override;
    void lower() override;

    void requestActivateWindow() override;

    bool setKeyboardGrabEnabled(bool grab) override { return grab; }
    bool setMouseGrabEnabled(bool grab) override;

    WId winId() const override;

    QSurfaceFormat format() const override;

    bool windowEvent(QEvent *event) override;

    void setMask(const QRegion &region) override;

    xcb_window_t xcb_window() const { return m_window; }
    uint depth() const { return m_depth; }
    QImage::Format imageFormat() const { return m_imageFormat; }
    bool imageNeedsRgbSwap() const { return m_imageRgbSwap; }

    bool handleNativeEvent(xcb_generic_event_t *event)  override;

    void handleExposeEvent(const xcb_expose_event_t *event) override;
    void handleClientMessageEvent(const xcb_client_message_event_t *event) override;
    void handleConfigureNotifyEvent(const xcb_configure_notify_event_t *event) override;
    void handleMapNotifyEvent(const xcb_map_notify_event_t *event) override;
    void handleUnmapNotifyEvent(const xcb_unmap_notify_event_t *event) override;
    void handleFocusInEvent(const xcb_focus_in_event_t *event) override;
    void handleFocusOutEvent(const xcb_focus_out_event_t *event) override;
    void handlePropertyNotifyEvent(const xcb_property_notify_event_t *event) override;
    void handleXIEnterLeave(xcb_ge_event_t *) override;

    MPlatformWindow *toWindow() override;

    void updateNetWmUserTime(xcb_timestamp_t timestamp);

    uint visualId() const;

    bool needsSync() const;

    void postSyncWindowRequest();
    void clearSyncWindowRequest() { m_pendingSyncRequest = nullptr; }

    MPlatformScreen *xcbScreen() const;

    virtual void create();
    virtual void destroy();

public Q_SLOTS:
    void updateSyncRequestCounter();

protected:
    void resolveFormat(const QSurfaceFormat &format) { m_format = format; }
    const xcb_visualtype_t *createVisual();
    void setImageFormatForVisual(const xcb_visualtype_t *visual);

    MPlatformScreen *parentScreen();
    MPlatformScreen *initialScreen() const;

    void setNetWmStateOnUnmappedWindow();

    void sendXEmbedMessage(xcb_window_t window, quint32 message,
                           quint32 detail = 0, quint32 data1 = 0, quint32 data2 = 0);
    void handleXEmbedMessage(const xcb_client_message_event_t *event);

    void show();
    void hide();

    bool relayFocusToModalWindow() const;
    void doFocusIn();
    void doFocusOut();

    void handleEnterNotifyEvent(int event_x, int event_y, int root_x, int root_y, xcb_timestamp_t timestamp);
    void handleLeaveNotifyEvent(int root_x, int root_y, xcb_timestamp_t timestamp);

    xcb_window_t m_window = 0;
    xcb_colormap_t m_cmap = 0;

    uint m_depth = 0;
    QImage::Format m_imageFormat = QImage::Format_ARGB32_Premultiplied;
    bool m_imageRgbSwap = false;

    xcb_sync_int64_t m_syncValue;
    xcb_sync_counter_t m_syncCounter = 0;

    Qt::WindowStates m_windowState = Qt::WindowNoState;

    bool m_mapped = false;
    bool m_transparent = false;
    bool m_deferredActivation = false;
    bool m_minimized = false;
    xcb_window_t m_netWmUserTimeWindow = XCB_NONE;

    QSurfaceFormat m_format;

    QRegion m_exposeRegion;
    QSize m_oldWindowSize;
    QPoint m_lastPointerPosition;

    xcb_visualid_t m_visualId = 0;
    // Last sent state. Initialized to an invalid state, on purpose.
    Qt::WindowStates m_lastWindowStateEvent = Qt::WindowActive;

    enum SyncState {
        NoSyncNeeded,
        SyncReceived,
        SyncAndConfigureReceived
    };
    SyncState m_syncState = NoSyncNeeded;

    MSyncWindowRequest *m_pendingSyncRequest = nullptr;

    qreal m_sizeHintsScaleFactor = 1.0;
};

QVector<xcb_rectangle_t> qRegionToXcbRectangleList(const QRegion &region);

Q_DECLARE_METATYPE(MPlatformWindow *)

#endif
