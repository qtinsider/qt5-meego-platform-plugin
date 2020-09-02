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

#include "xcbatom.h"

#include <QtCore/qglobal.h>

#include <string.h>

#include <algorithm>

static const char *xcb_atomnames = {
    // window-manager <-> client protocols
    "WM_PROTOCOLS\0"
    "WM_DELETE_WINDOW\0"
    "WM_TAKE_FOCUS\0"
    "_NET_WM_PING\0"
    "_NET_WM_SYNC_REQUEST\0"
    "_NET_WM_SYNC_REQUEST_COUNTER\0"

    // ICCCM window state
    "WM_STATE\0"
    "WM_CHANGE_STATE\0"
    "WM_CLASS\0"
    "WM_NAME\0"

    // Session management
    "WM_CLIENT_LEADER\0"
    "WM_WINDOW_ROLE\0"
    "SM_CLIENT_ID\0"
    "WM_CLIENT_MACHINE\0"

    // Clipboard
    "CLIPBOARD\0"
    "INCR\0"
    "TARGETS\0"
    "MULTIPLE\0"
    "TIMESTAMP\0"
    "SAVE_TARGETS\0"
    "CLIP_TEMPORARY\0"
    "_QT_SELECTION\0"
    "_QT_CLIPBOARD_SENTINEL\0"
    "_QT_SELECTION_SENTINEL\0"
    "CLIPBOARD_MANAGER\0"

    "RESOURCE_MANAGER\0"

    "_XSETROOT_ID\0"

    "_QT_SCROLL_DONE\0"
    "_QT_INPUT_ENCODING\0"

    "_QT_CLOSE_CONNECTION\0"

    "_MOTIF_WM_HINTS\0"

    // EWMH (aka NETWM)
    "_NET_SUPPORTED\0"
    "_NET_WORKAREA\0"

    "_NET_WM_NAME\0"
    "_NET_WM_ICON_NAME\0"
    "_NET_WM_ICON\0"

    "_NET_WM_PID\0"

    "_NET_WM_WINDOW_OPACITY\0"

    "_NET_WM_STATE\0"
    "_NET_WM_STATE_FULLSCREEN\0"

    "_NET_WM_USER_TIME\0"
    "_NET_WM_USER_TIME_WINDOW\0"
    "_NET_WM_FULL_PLACEMENT\0"

    "_NET_STARTUP_INFO\0"
    "_NET_STARTUP_INFO_BEGIN\0"

    "_NET_SUPPORTING_WM_CHECK\0"

    "_NET_WM_CM_S0\0"

    "_NET_ACTIVE_WINDOW\0"

    // Property formats
    "TEXT\0"
    "UTF8_STRING\0"
    "CARDINAL\0"

    // XEMBED
    "_XEMBED\0"
    "_XEMBED_INFO\0"

    // XInput2
    "Abs MT Position X\0"
    "Abs MT Position Y\0"
    "Abs MT Touch Major\0"
    "Abs MT Touch Minor\0"
    "Abs MT Tracking ID\0"
    "Max Contacts\0"

    "_MEEGOTOUCH_MINIMIZE_ANIMATION\0"
    "_MEEGOTOUCH_ORIENTATION_ANGLE\0"
    // \0\0 terminates loop.
};

XcbAtom::XcbAtom()
{
}

void XcbAtom::initialize(xcb_connection_t *connection)
{
    initializeAllAtoms(connection);
}

void XcbAtom::initializeAllAtoms(xcb_connection_t *connection) {
    const char *names[XcbAtom::NAtoms];
    const char *ptr = xcb_atomnames;

    int i = 0;
    while (*ptr) {
        names[i++] = ptr;
        while (*ptr)
            ++ptr;
        ++ptr;
    }

    Q_ASSERT(i == XcbAtom::NAtoms);

    xcb_intern_atom_cookie_t cookies[XcbAtom::NAtoms];

    Q_ASSERT(i == XcbAtom::NAtoms);
    for (i = 0; i < XcbAtom::NAtoms; ++i)
        cookies[i] = xcb_intern_atom(connection, false, strlen(names[i]), names[i]);

    for (i = 0; i < XcbAtom::NAtoms; ++i) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(connection, cookies[i], nullptr);
        m_allAtoms[i] = reply->atom;
        free(reply);
    }
}

XcbAtom::Atom XcbAtom::qatom(xcb_atom_t xatom) const
{
    return static_cast<XcbAtom::Atom>(std::find(m_allAtoms, m_allAtoms + XcbAtom::NAtoms, xatom) - m_allAtoms);
}
