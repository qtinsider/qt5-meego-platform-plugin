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

#include "meventdispatcher.h"
#include "xcbconnection.h"

#include <QtCore/QCoreApplication>

#include <qpa/qwindowsysteminterface.h>

struct MEventSource
{
    GSource source;
    MGlibEventDispatcher *dispatcher;
    MGlibEventDispatcherPrivate *dispatcher_p;
    XcbConnection *connection = nullptr;
};

static gboolean meegoSourcePrepare(GSource *source, gint *timeout)
{
    Q_UNUSED(timeout)
    auto meegoEventSource = reinterpret_cast<MEventSource *>(source);
    return meegoEventSource->dispatcher_p->wakeUpCalled;
}

static gboolean meegoSourceCheck(GSource *source)
{
    return meegoSourcePrepare(source, nullptr);
}

static gboolean meegoSourceDispatch(GSource *source, GSourceFunc, gpointer)
{
    auto xcbEventSource = reinterpret_cast<MEventSource *>(source);
    QEventLoop::ProcessEventsFlags flags = xcbEventSource->dispatcher->flags();
    xcbEventSource->connection->processXcbEvents(flags);
    // The following line should not be necessary after QTBUG-70095
    QWindowSystemInterface::sendWindowSystemEvents(flags);
    return true;
}

MGlibEventDispatcher::MGlibEventDispatcher(XcbConnection *connection, QObject *parent)
    : QEventDispatcherGlib(*new MGlibEventDispatcherPrivate(), parent)
{
    Q_D(MGlibEventDispatcher);

    m_mEventSourceFuncs.prepare = meegoSourcePrepare;
    m_mEventSourceFuncs.check = meegoSourceCheck;
    m_mEventSourceFuncs.dispatch = meegoSourceDispatch;
    m_mEventSourceFuncs.finalize = nullptr;

    m_mEventSource = reinterpret_cast<MEventSource *>(
                g_source_new(&m_mEventSourceFuncs, sizeof(MEventSource)));

    m_mEventSource->dispatcher = this;
    m_mEventSource->dispatcher_p = d_func();
    m_mEventSource->connection = connection;

    g_source_set_can_recurse(&m_mEventSource->source, true);
    g_source_attach(&m_mEventSource->source, d->mainContext);
}

MGlibEventDispatcherPrivate::MGlibEventDispatcherPrivate()
{
}

MGlibEventDispatcher::~MGlibEventDispatcher()
{
    g_source_destroy(&m_mEventSource->source);
    g_source_unref(&m_mEventSource->source);
}

bool MGlibEventDispatcher::processEvents(QEventLoop::ProcessEventsFlags flags)
{
    m_flags = flags;
    return QEventDispatcherGlib::processEvents(m_flags);
}

QAbstractEventDispatcher *MEventDispatcher::createEventDispatcher(XcbConnection *connection)
{
    qCDebug(lcQpaXcb, "using glib dispatcher");
    return new MGlibEventDispatcher(connection);
}
