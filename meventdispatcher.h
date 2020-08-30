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

#ifndef MEVENTDISPATCHER_H
#define MEVENTDISPATCHER_H

#include <QtCore/QObject>
#include <QtCore/QEventLoop>

#include <QtCore/private/qeventdispatcher_glib_p.h>
#include <glib.h>

class XcbConnection;

struct MEventSource;
class MGlibEventDispatcherPrivate;

class MGlibEventDispatcher : public QEventDispatcherGlib
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(MGlibEventDispatcher)

public:
    explicit MGlibEventDispatcher(XcbConnection *connection, QObject *parent = nullptr);
    ~MGlibEventDispatcher();

    bool processEvents(QEventLoop::ProcessEventsFlags flags) override;
    QEventLoop::ProcessEventsFlags flags() const { return m_flags; }

private:
    MEventSource *m_mEventSource;
    GSourceFuncs m_mEventSourceFuncs;
    QEventLoop::ProcessEventsFlags m_flags;
};

class MGlibEventDispatcherPrivate : public QEventDispatcherGlibPrivate
{
    Q_DECLARE_PUBLIC(MGlibEventDispatcher)

public:
    MGlibEventDispatcherPrivate();
};

class MEventDispatcher
{
public:
    static QAbstractEventDispatcher *createEventDispatcher(XcbConnection *connection);
};

#endif // MEVENTDISPATCHER_H
