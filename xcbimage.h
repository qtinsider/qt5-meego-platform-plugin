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

#ifndef XCBIMAGE_H
#define XCBIMAGE_H

#include "mplatformscreen.h"
#include <QtCore/QPair>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <xcb/xcb_image.h>

bool qt_xcb_imageFormatForVisual(XcbConnection *connection, uint8_t depth, const xcb_visualtype_t *visual,
                                 QImage::Format *imageFormat, bool *needsRgbSwap = nullptr);
QPixmap qt_xcb_pixmapFromXPixmap(XcbConnection *connection, xcb_pixmap_t pixmap,
                                 int width, int height, int depth,
                                 const xcb_visualtype_t *visual);
xcb_pixmap_t qt_xcb_XPixmapFromBitmap(MPlatformScreen *screen, const QImage &image);

#endif
