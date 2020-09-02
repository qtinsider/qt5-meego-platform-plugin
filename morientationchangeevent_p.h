/*
 * Copyright (C) 2020 Chukwudi Nwutobo <nwutobo@outlook.com>
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


#ifndef MORIENTATIONCHANGEEVENT_P_H
#define MORIENTATIONCHANGEEVENT_P_H

#include <QEvent>

class OrientationChangeEvent : public QEvent {
public:
    enum Orientation { TopUp, LeftUp, TopDown, RightUp };

    OrientationChangeEvent(Orientation orientation)
        : QEvent()
        , m_orientation(orientation)
    {
    }

    Orientation m_orientation;
};

#endif // MORIENTATIONCHANGEEVENT_P_H
