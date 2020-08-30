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

#include "xcbwmsupport.h"
#include "mplatformscreen.h"

#include <qdebug.h>

XcbWMSupport::XcbWMSupport(XcbConnection *c)
    : XcbObject(c)
{
    updateNetWMAtoms();
}

bool XcbWMSupport::isSupportedByWM(xcb_atom_t atom) const
{
    return net_wm_atoms.contains(atom);
}

void XcbWMSupport::updateNetWMAtoms()
{
    net_wm_atoms.clear();

    xcb_window_t root = connection()->primaryScreen()->root();
    int offset = 0;
    int remaining = 0;
    do {
        auto reply = Q_XCB_REPLY(xcb_get_property, xcb_connection(), false, root, atom(XcbAtom::_NET_SUPPORTED), XCB_ATOM_ATOM, offset, 1024);
        if (!reply)
            break;

        remaining = 0;

        if (reply->type == XCB_ATOM_ATOM && reply->format == 32) {
            int len = xcb_get_property_value_length(reply.get()) / sizeof(xcb_atom_t);
            xcb_atom_t *atoms = (xcb_atom_t *) xcb_get_property_value(reply.get());
            int s = net_wm_atoms.size();
            net_wm_atoms.resize(s + len);
            memcpy(net_wm_atoms.data() + s, atoms, len * sizeof(xcb_atom_t));

            remaining = reply->bytes_after;
            offset += len;
        }
    } while (remaining > 0);

//#define NET_WM_ATOMS_DEBUG
#ifdef NET_WM_ATOMS_DEBUG
    qDebug("======== updateNetWMAtoms");
    for (int i = 0; i < net_wm_atoms.size(); ++i)
        qDebug() << connection()->atomName(net_wm_atoms.at(i));
    qDebug("======== updateNetWMAtoms");
#endif
}
