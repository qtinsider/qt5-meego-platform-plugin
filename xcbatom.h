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

#ifndef XCBATOM_H
#define XCBATOM_H

#include <xcb/xcb.h>

class XcbAtom
{
public:
    enum Atom {
        // window-manager <-> client protocols
        WM_PROTOCOLS,
        WM_DELETE_WINDOW,
        WM_TAKE_FOCUS,
        _NET_WM_PING,
        _NET_WM_SYNC_REQUEST,
        _NET_WM_SYNC_REQUEST_COUNTER,

        // ICCCM window state
        WM_STATE,
        WM_CHANGE_STATE,
        WM_CLASS,
        WM_NAME,

        // Session management
        WM_CLIENT_LEADER,
        WM_WINDOW_ROLE,
        SM_CLIENT_ID,
        WM_CLIENT_MACHINE,

        // Clipboard
        CLIPBOARD,
        INCR,
        TARGETS,
        MULTIPLE,
        TIMESTAMP,
        SAVE_TARGETS,
        CLIP_TEMPORARY,
        _QT_SELECTION,
        _QT_CLIPBOARD_SENTINEL,
        _QT_SELECTION_SENTINEL,
        CLIPBOARD_MANAGER,

        RESOURCE_MANAGER,

        _XSETROOT_ID,

        _QT_SCROLL_DONE,
        _QT_INPUT_ENCODING,

        // Qt/XCB specific
        _QT_CLOSE_CONNECTION,

        _MOTIF_WM_HINTS,

        // EWMH (aka NETWM)
        _NET_SUPPORTED,
        _NET_WORKAREA,

        _NET_WM_NAME,
        _NET_WM_ICON_NAME,
        _NET_WM_ICON,

        _NET_WM_PID,

        _NET_WM_WINDOW_OPACITY,

        _NET_WM_STATE,
        _NET_WM_STATE_FULLSCREEN,

        _NET_WM_USER_TIME,
        _NET_WM_USER_TIME_WINDOW,
        _NET_WM_FULL_PLACEMENT,

        _NET_STARTUP_INFO,
        _NET_STARTUP_INFO_BEGIN,

        _NET_SUPPORTING_WM_CHECK,

        _NET_WM_CM_S0,

        _NET_ACTIVE_WINDOW,

        // Property formats
        TEXT,
        UTF8_STRING,
        CARDINAL,

        // XEMBED
        _XEMBED,
        _XEMBED_INFO,

        // XInput2
        AbsMTPositionX,
        AbsMTPositionY,
        AbsMTTouchMajor,
        AbsMTTouchMinor,
        AbsMTTrackingID,
        MaxContacts,

        // MEEGO(TOUCH)-specific
        _MEEGOTOUCH_MINIMIZE_ANIMATION,
        _MEEGOTOUCH_ORIENTATION_ANGLE,

        NAtoms
    };

    XcbAtom();
    void initialize(xcb_connection_t *connection);

    inline xcb_atom_t atom(XcbAtom::Atom atom) const { return m_allAtoms[atom]; }
    XcbAtom::Atom qatom(xcb_atom_t atom) const;

protected:
    void initializeAllAtoms(xcb_connection_t *connection);

private:
    xcb_atom_t m_allAtoms[XcbAtom::NAtoms];
};

#endif // XCBATOM_H
