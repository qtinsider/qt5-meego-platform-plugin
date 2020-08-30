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

#ifndef XCBMIME_H
#define XCBMIME_H

#include <QtGui/private/qinternalmimedata_p.h>

#include <QtGui/QClipboard>

#include "mplatformintegration.h"
#include "xcbconnection.h"

class XcbMime : public QInternalMimeData {
    Q_OBJECT
public:
    XcbMime();
    ~XcbMime();

    static QVector<xcb_atom_t> mimeAtomsForFormat(XcbConnection *connection, const QString &format);
    static QString mimeAtomToString(XcbConnection *connection, xcb_atom_t a);
    static bool mimeDataForAtom(XcbConnection *connection, xcb_atom_t a, QMimeData *mimeData, QByteArray *data,
                                xcb_atom_t *atomFormat, int *dataFormat);
    static QVariant mimeConvertToFormat(XcbConnection *connection, xcb_atom_t a, const QByteArray &data, const QString &format,
                                        QMetaType::Type requestedType, const QByteArray &encoding);
    static xcb_atom_t mimeAtomForFormat(XcbConnection *connection, const QString &format, QMetaType::Type requestedType,
                                        const QVector<xcb_atom_t> &atoms, QByteArray *requestedEncoding);
};

#endif // XCBMIME_H
