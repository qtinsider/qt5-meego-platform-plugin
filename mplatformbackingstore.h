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

#ifndef MPLATFORMBACKINGSTORE_H
#define MPLATFORMBACKINGSTORE_H

#include <qpa/qplatformbackingstore.h>
#include <QtCore/QStack>

#include <xcb/xcb.h>

#include "xcbobject.h"

class XcbBackingStoreImage;

class MPlatformBackingStore : public XcbObject, public QPlatformBackingStore
{
public:
    MPlatformBackingStore(QWindow *window);
    ~MPlatformBackingStore();

    QPaintDevice *paintDevice() override;
    void flush(QWindow *window, const QRegion &region, const QPoint &offset) override;
    void composeAndFlush(QWindow *window, const QRegion &region, const QPoint &offset,
                         QPlatformTextureList *textures,
                         bool translucentBackground) override;
    QImage toImage() const override;

    QPlatformGraphicsBuffer *graphicsBuffer() const override;

    void resize(const QSize &size, const QRegion &staticContents) override;
    bool scroll(const QRegion &area, int dx, int dy) override;

    void beginPaint(const QRegion &) override;
    void endPaint() override;

    static bool createSystemVShmSegment(xcb_connection_t *c, size_t segmentSize = 1,
                                        void *shmInfo = nullptr);

protected:
    virtual void render(xcb_window_t window, const QRegion &region, const QPoint &offset);
    virtual void recreateImage(MPlatformWindow *win, const QSize &size);

    XcbBackingStoreImage *m_image = nullptr;
    QStack<QRegion> m_paintRegions;
    QImage m_rgbImage;
};

#endif
