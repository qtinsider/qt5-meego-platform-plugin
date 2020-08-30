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

#ifndef XCBWMSUPPORT_H
#define XCBWMSUPPORT_H

#include "xcbobject.h"
#include "xcbconnection.h"
#include <qvector.h>

class XcbWMSupport : public XcbObject
{
public:
    XcbWMSupport(XcbConnection *c);

    bool isSupportedByWM(xcb_atom_t atom) const;

private:
    friend class XcbConnection;
    void updateNetWMAtoms();

    QVector<xcb_atom_t> net_wm_atoms;
};

#endif
