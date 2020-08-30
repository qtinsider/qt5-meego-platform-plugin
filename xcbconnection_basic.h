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

#ifndef XCBBASICCONNECTION_H
#define XCBBASICCONNECTION_H

#include "xcbatom.h"
#include "mexport.h"

#include <QtCore/QPair>
#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QLoggingCategory>
#include <QtGui/private/qtguiglobal_p.h>

#include <xcb/xcb.h>

#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcQpaXcb)

class M_EXPORT XcbBasicConnection : public QObject
{
    Q_OBJECT
public:
    XcbBasicConnection(const char *displayName);
    ~XcbBasicConnection();

    void *xlib_display() const { return m_xlibDisplay; }

    const char *displayName() const { return m_displayName.constData(); }
    xcb_connection_t *xcb_connection() const { return m_xcbConnection; }
    bool isConnected() const {
        return m_xcbConnection && !xcb_connection_has_error(m_xcbConnection);
    }
    const xcb_setup_t *setup() const { return m_setup; }

    size_t maxRequestDataBytes(size_t requestSize) const;

    inline xcb_atom_t atom(XcbAtom::Atom qatom) const { return m_xcbAtom.atom(qatom); }
    XcbAtom::Atom qatom(xcb_atom_t atom) const { return m_xcbAtom.qatom(atom); }
    xcb_atom_t internAtom(const char *name);
    QByteArray atomName(xcb_atom_t atom);

    bool hasXFixes() const { return m_hasXFixes; }
    bool hasXShape() const { return m_hasXShape; }
    bool hasInputShape() const { return m_hasInputShape; }
    bool hasXRender(int major = -1, int minor = -1) const {
        if (m_hasXRender && major != -1 && minor != -1)
            return m_xrenderVersion >= qMakePair(major, minor);

        return m_hasXRender;
    }
    bool hasXInput2() const { return m_xi2Enabled; }
    bool hasShm() const { return m_hasShm; }
    bool hasBigRequest() const;

    bool isXIEvent(xcb_generic_event_t *event) const;
    bool isXIType(xcb_generic_event_t *event, uint16_t type) const;

    bool isXFixesType(uint responseType, int eventType) const;

protected:
    void initializeShm();
    void initializeXFixes();
    void initializeXRender();
    void initializeXShape();
    void initializeXSync();
    void initializeXInput2();

private:
    void *m_xlibDisplay = nullptr;

    QByteArray m_displayName;
    xcb_connection_t *m_xcbConnection = nullptr;
    const xcb_setup_t *m_setup = nullptr;
    XcbAtom m_xcbAtom;

    bool m_hasXFixes = false;
    bool m_hasXShape = false;
    bool m_hasInputShape;
    bool m_hasXRender = false;
    bool m_hasShm = false;

    QPair<int, int> m_xrenderVersion;

    bool m_xi2Enabled = false;
    int m_xiOpCode = -1;
    uint32_t m_xinputFirstEvent = 0;

    uint32_t m_xfixesFirstEvent = 0;

    uint32_t m_maximumRequestLength = 0;
};

#define Q_XCB_REPLY_CONNECTION_ARG(connection, ...) connection

struct QStdFreeDeleter {
    void operator()(void *p) const noexcept { return std::free(p); }
};

#define Q_XCB_REPLY(call, ...) \
    std::unique_ptr<call##_reply_t, QStdFreeDeleter>( \
        call##_reply(Q_XCB_REPLY_CONNECTION_ARG(__VA_ARGS__), call(__VA_ARGS__), nullptr) \
    )

#define Q_XCB_REPLY_UNCHECKED(call, ...) \
    std::unique_ptr<call##_reply_t, QStdFreeDeleter>( \
        call##_reply(Q_XCB_REPLY_CONNECTION_ARG(__VA_ARGS__), call##_unchecked(__VA_ARGS__), nullptr) \
    )

#endif // XCBBASICCONNECTION_H
