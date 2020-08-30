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

#ifndef MPLATFORMSCREEN_H
#define MPLATFORMSCREEN_H

#include <qpa/qplatformscreen.h>
#include <QtCore/QString>

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xfixes.h>

#include "xcbobject.h"

#include <private/qfontengine_p.h>

class XcbConnection;
#ifndef QT_NO_DEBUG_STREAM
class QDebug;
#endif

class M_EXPORT MPlatformScreen : public XcbObject, public QPlatformScreen
{
public:
    MPlatformScreen(XcbConnection *connection, xcb_screen_t *screen);
    ~MPlatformScreen();

    QString name() const override { return m_outputName; }

    QRect geometry() const override;
    QRect availableGeometry() const override;
    int depth() const override;
    QImage::Format format() const override;
    QSizeF physicalSize() const override;
    QDpi logicalDpi() const override;

    Qt::ScreenOrientation orientation() const override { return m_orientation; }

    QPixmap grabWindow(WId window, int xIn, int yIn, int width, int height) const override;

    xcb_screen_t *screen() const { return m_screen; }
    xcb_window_t root() const { return m_screen->root; }

    void windowShown(MPlatformWindow *window);

    void updateProperties();

    QSurfaceFormat surfaceFormatFor(const QSurfaceFormat &format) const;

    const xcb_visualtype_t *visualForFormat(const QSurfaceFormat &format) const;
    const xcb_visualtype_t *visualForId(xcb_visualid_t visualid) const;
    quint8 depthOfVisual(xcb_visualid_t visualid) const;

private:
    void sendStartupMessage(const QByteArray &message) const;

    xcb_screen_t *m_screen;

    QMap<xcb_visualid_t, xcb_visualtype_t> m_visuals;
    QMap<xcb_visualid_t, quint8> m_visualDepths;
    uint16_t m_rotation = 0;

    QString m_outputName;
    Qt::ScreenOrientation m_orientation = Qt::PrimaryOrientation;
};

#ifndef QT_NO_DEBUG_STREAM
Q_GUI_EXPORT QDebug operator<<(QDebug, const MPlatformScreen *);
#endif

#endif // MPLATFORMSCREEN_H
