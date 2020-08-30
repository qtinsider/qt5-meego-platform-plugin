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

#include "xcbmime.h"

#include <QtCore/QTextCodec>
#include <QtGui/QImageWriter>
#include <QtCore/QBuffer>
#include <qdebug.h>

XcbMime::XcbMime()
    : QInternalMimeData()
{ }

XcbMime::~XcbMime()
{}



QString XcbMime::mimeAtomToString(XcbConnection *connection, xcb_atom_t a)
{
    if (a == XCB_NONE)
        return QString();

    // special cases for string type
    if (a == XCB_ATOM_STRING
        || a == connection->atom(XcbAtom::UTF8_STRING)
        || a == connection->atom(XcbAtom::TEXT))
        return QLatin1String("text/plain");

    // special case for images
    if (a == XCB_ATOM_PIXMAP)
        return QLatin1String("image/ppm");

    QByteArray atomName = connection->atomName(a);

    // special cases for uris
    if (atomName == "text/x-moz-url")
        atomName = "text/uri-list";

    return QString::fromLatin1(atomName.constData());
}

bool XcbMime::mimeDataForAtom(XcbConnection *connection, xcb_atom_t a, QMimeData *mimeData, QByteArray *data,
                               xcb_atom_t *atomFormat, int *dataFormat)
{
    if (!data)
        return false;

    bool ret = false;
    *atomFormat = a;
    *dataFormat = 8;

    if ((a == connection->atom(XcbAtom::UTF8_STRING)
         || a == XCB_ATOM_STRING
         || a == connection->atom(XcbAtom::TEXT))
        && QInternalMimeData::hasFormatHelper(QLatin1String("text/plain"), mimeData)) {
        if (a == connection->atom(XcbAtom::UTF8_STRING)) {
            *data = QInternalMimeData::renderDataHelper(QLatin1String("text/plain"), mimeData);
            ret = true;
        } else if (a == XCB_ATOM_STRING ||
                   a == connection->atom(XcbAtom::TEXT)) {
            // ICCCM says STRING is latin1
            *data = QString::fromUtf8(QInternalMimeData::renderDataHelper(
                        QLatin1String("text/plain"), mimeData)).toLatin1();
            ret = true;
        }
        return ret;
    }

    QString atomName = mimeAtomToString(connection, a);
    if (QInternalMimeData::hasFormatHelper(atomName, mimeData)) {
        *data = QInternalMimeData::renderDataHelper(atomName, mimeData);
        // mimeAtomToString() converts "text/x-moz-url" to "text/uri-list",
        // so MConnection::atomName() has to be used.
        if (atomName == QLatin1String("text/uri-list")
            && connection->atomName(a) == "text/x-moz-url") {
            const QString mozUri = QLatin1String(data->split('\n').constFirst()) + QLatin1Char('\n');
            *data = QByteArray(reinterpret_cast<const char *>(mozUri.utf16()),
                               mozUri.length() * 2);
        } else if (atomName == QLatin1String("application/x-color"))
            *dataFormat = 16;
        ret = true;
    } else if ((a == XCB_ATOM_PIXMAP || a == XCB_ATOM_BITMAP) && mimeData->hasImage()) {
        ret = true;
    } else if (atomName == QLatin1String("text/plain")
               && mimeData->hasFormat(QLatin1String("text/uri-list"))) {
        // Return URLs also as plain text.
        *data = QInternalMimeData::renderDataHelper(atomName, mimeData);
        ret = true;
    }
    return ret;
}

QVector<xcb_atom_t> XcbMime::mimeAtomsForFormat(XcbConnection *connection, const QString &format)
{
    QVector<xcb_atom_t> atoms;
    atoms.reserve(7);
    atoms.append(connection->internAtom(format.toLatin1()));

    // special cases for strings
    if (format == QLatin1String("text/plain")) {
        atoms.append(connection->atom(XcbAtom::UTF8_STRING));
        atoms.append(XCB_ATOM_STRING);
        atoms.append(connection->atom(XcbAtom::TEXT));
    }

    // special cases for uris
    if (format == QLatin1String("text/uri-list")) {
        atoms.append(connection->internAtom("text/x-moz-url"));
        atoms.append(connection->internAtom("text/plain"));
    }

    //special cases for images
    if (format == QLatin1String("image/ppm"))
        atoms.append(XCB_ATOM_PIXMAP);
    if (format == QLatin1String("image/pbm"))
        atoms.append(XCB_ATOM_BITMAP);

    return atoms;
}

QVariant XcbMime::mimeConvertToFormat(XcbConnection *connection, xcb_atom_t a, const QByteArray &d, const QString &format,
                                       QMetaType::Type requestedType, const QByteArray &encoding)
{
    QByteArray data = d;
    QString atomName = mimeAtomToString(connection, a);
//    qDebug() << "mimeConvertDataToFormat" << format << atomName << data;

    if (!encoding.isEmpty()
        && atomName == format + QLatin1String(";charset=") + QLatin1String(encoding)) {

#if QT_CONFIG(textcodec)
        if (requestedType == QMetaType::QString) {
            QTextCodec *codec = QTextCodec::codecForName(encoding);
            if (codec)
                return codec->toUnicode(data);
        }
#endif

        return data;
    }

    // special cases for string types
    if (format == QLatin1String("text/plain")) {
        if (data.endsWith('\0'))
            data.chop(1);
        if (a == connection->atom(XcbAtom::UTF8_STRING)) {
            return QString::fromUtf8(data);
        }
        if (a == XCB_ATOM_STRING ||
            a == connection->atom(XcbAtom::TEXT))
            return QString::fromLatin1(data);
    }
    // If data contains UTF16 text, convert it to a string.
    // Firefox uses UTF16 without BOM for text/x-moz-url, "text/html",
    // Google Chrome uses UTF16 without BOM for "text/x-moz-url",
    // UTF16 with BOM for "text/html".
    if ((format == QLatin1String("text/html") || format == QLatin1String("text/uri-list"))
        && data.size() > 1) {
        const quint8 byte0 = data.at(0);
        const quint8 byte1 = data.at(1);
        if ((byte0 == 0xff && byte1 == 0xfe) || (byte0 == 0xfe && byte1 == 0xff)
            || (byte0 != 0 && byte1 == 0) || (byte0 == 0 && byte1 != 0)) {
            const QString str = QString::fromUtf16(
                  reinterpret_cast<const ushort *>(data.constData()), data.size() / 2);
            if (!str.isNull()) {
                if (format == QLatin1String("text/uri-list")) {
                    const auto urls = str.splitRef(QLatin1Char('\n'));
                    QList<QVariant> list;
                    list.reserve(urls.size());
                    for (const QStringRef &s : urls) {
                        const QUrl url(s.trimmed().toString());
                        if (url.isValid())
                            list.append(url);
                    }
                    // We expect "text/x-moz-url" as <url><space><title>.
                    // The atomName variable is not used because mimeAtomToString()
                    // converts "text/x-moz-url" to "text/uri-list".
                    if (!list.isEmpty() && connection->atomName(a) == "text/x-moz-url")
                        return list.constFirst();
                    return list;
                } else {
                    return str;
                }
            }
        }
        // 8 byte encoding, remove a possible 0 at the end
        if (data.endsWith('\0'))
            data.chop(1);
    }

    if (atomName == format)
        return data;

#if 0 // ###
    // special case for images
    if (format == QLatin1String("image/ppm")) {
        if (a == XCB_ATOM_PIXMAP && data.size() == sizeof(Pixmap)) {
            Pixmap xpm = *((Pixmap*)data.data());
            if (!xpm)
                return QByteArray();
            Window root;
            int x;
            int y;
            uint width;
            uint height;
            uint border_width;
            uint depth;

            XGetGeometry(display, xpm, &root, &x, &y, &width, &height, &border_width, &depth);
            XImage *ximg = XGetImage(display,xpm,x,y,width,height,AllPlanes,depth==1 ? XYPixmap : ZPixmap);
            QImage qimg = QXlibStatic::qimageFromXImage(ximg);
            XDestroyImage(ximg);

            QImageWriter imageWriter;
            imageWriter.setFormat("PPMRAW");
            QBuffer buf;
            buf.open(QIODevice::WriteOnly);
            imageWriter.setDevice(&buf);
            imageWriter.write(qimg);
            return buf.buffer();
        }
    }
#endif
    return QVariant();
}

xcb_atom_t XcbMime::mimeAtomForFormat(XcbConnection *connection, const QString &format, QMetaType::Type requestedType,
                                 const QVector<xcb_atom_t> &atoms, QByteArray *requestedEncoding)
{
    requestedEncoding->clear();

    // find matches for string types
    if (format == QLatin1String("text/plain")) {
        if (atoms.contains(connection->atom(XcbAtom::UTF8_STRING)))
            return connection->atom(XcbAtom::UTF8_STRING);
        if (atoms.contains(XCB_ATOM_STRING))
            return XCB_ATOM_STRING;
        if (atoms.contains(connection->atom(XcbAtom::TEXT)))
            return connection->atom(XcbAtom::TEXT);
    }

    // find matches for uri types
    if (format == QLatin1String("text/uri-list")) {
        xcb_atom_t a = connection->internAtom(format.toLatin1());
        if (a && atoms.contains(a))
            return a;
        a = connection->internAtom("text/x-moz-url");
        if (a && atoms.contains(a))
            return a;
    }

    // find match for image
    if (format == QLatin1String("image/ppm")) {
        if (atoms.contains(XCB_ATOM_PIXMAP))
            return XCB_ATOM_PIXMAP;
    }

    // for string/text requests try to use a format with a well-defined charset
    // first to avoid encoding problems
    if (requestedType == QMetaType::QString
        && format.startsWith(QLatin1String("text/"))
        && !format.contains(QLatin1String("charset="))) {

        QString formatWithCharset = format;
        formatWithCharset.append(QLatin1String(";charset=utf-8"));

        xcb_atom_t a = connection->internAtom(std::move(formatWithCharset).toLatin1());
        if (a && atoms.contains(a)) {
            *requestedEncoding = "utf-8";
            return a;
        }
    }

    xcb_atom_t a = connection->internAtom(format.toLatin1());
    if (a && atoms.contains(a))
        return a;

    return 0;
}
