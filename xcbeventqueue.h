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

#ifndef XCBEVENTQUEUE_H
#define XCBEVENTQUEUE_H

#include <QtCore/QThread>
#include <QtCore/QHash>
#include <QtCore/QEventLoop>
#include <QtCore/QVector>
#include <QtCore/QMutex>
#include <QtCore/QWaitCondition>

#include <xcb/xcb.h>

#include <atomic>

struct XcbEventNode {
    XcbEventNode(xcb_generic_event_t *e = nullptr)
        : event(e) { }

    xcb_generic_event_t *event;
    XcbEventNode *next = nullptr;
    bool fromHeap = false;
};

class XcbConnection;
class QAbstractEventDispatcher;

class XcbEventQueue : public QThread
{
    Q_OBJECT
public:
    XcbEventQueue(XcbConnection *connection);
    ~XcbEventQueue();

    enum { PoolSize = 100 }; // 2.4 kB with 100 nodes

    enum PeekOption {
        PeekDefault = 0, // see qx11info_x11.h for docs
        PeekFromCachedIndex = 1,
        PeekRetainMatch = 2,
        PeekRemoveMatch = 3,
        PeekRemoveMatchContinue = 4
    };
    Q_DECLARE_FLAGS(PeekOptions, PeekOption)

    void run() override;

    bool isEmpty() const { return m_head == m_flushedTail && !m_head->event; }
    xcb_generic_event_t *takeFirst(QEventLoop::ProcessEventsFlags flags);
    xcb_generic_event_t *takeFirst();
    void flushBufferedEvents();
    void wakeUpDispatcher();

    // ### peek() and peekEventQueue() could be unified. Note that peekEventQueue()
    // is public API exposed via QX11Extras/QX11Info.
    template<typename Peeker>
    xcb_generic_event_t *peek(Peeker &&peeker) {
        return peek(PeekRemoveMatch, std::forward<Peeker>(peeker));
    }
    template<typename Peeker>
    inline xcb_generic_event_t *peek(PeekOption config, Peeker &&peeker);

    qint32 generatePeekerId();
    bool removePeekerId(qint32 peekerId);

    using PeekerCallback = bool (*)(xcb_generic_event_t *event, void *peekerData);
    bool peekEventQueue(PeekerCallback peeker, void *peekerData = nullptr,
                        PeekOptions option = PeekDefault, qint32 peekerId = -1);

    void waitForNewEvents(unsigned long time = ULONG_MAX);

private:
    XcbEventNode *qMEventNodeFactory(xcb_generic_event_t *event);
    void dequeueNode();

    void sendCloseConnectionEvent() const;
    bool isCloseConnectionEvent(const xcb_generic_event_t *event);

    XcbEventNode *m_head = nullptr;
    XcbEventNode *m_flushedTail = nullptr;
    std::atomic<XcbEventNode *> m_tail { nullptr };
    std::atomic_uint m_nodesRestored { 0 };

    XcbConnection *m_connection = nullptr;
    bool m_closeConnectionDetected = false;

    uint m_freeNodes = PoolSize;
    uint m_poolIndex = 0;

    qint32 m_peekerIdSource = 0;
    bool m_queueModified = false;
    bool m_peekerIndexCacheDirty = false;
    QHash<qint32, XcbEventNode *> m_peekerToNode;

    QVector<xcb_generic_event_t *> m_inputEvents;

    // debug stats
    quint64 m_nodesOnHeap = 0;

    QMutex m_newEventsMutex;
    QWaitCondition m_newEventsCondition;
};

template<typename Peeker>
xcb_generic_event_t *XcbEventQueue::peek(PeekOption option, Peeker &&peeker)
{
    flushBufferedEvents();
    if (isEmpty())
        return nullptr;

    XcbEventNode *node = m_head;
    do {
        xcb_generic_event_t *event = node->event;
        if (event && peeker(event, event->response_type & ~0x80)) {
            if (option == PeekRemoveMatch || option == PeekRemoveMatchContinue)
                node->event = nullptr;
            if (option != PeekRemoveMatchContinue)
                return event;
        }
        if (node == m_flushedTail)
            break;
        node = node->next;
    } while (true);

    return nullptr;
}

#endif
