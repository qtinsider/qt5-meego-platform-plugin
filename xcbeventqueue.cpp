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

#include "xcbeventqueue.h"
#include "xcbconnection.h"

#include <QtCore/QObject>
#include <QtCore/QCoreApplication>
#include <QtCore/QAbstractEventDispatcher>
#include <QtCore/QMutex>
#include <QtCore/QDebug>

static QBasicMutex qAppExiting;
static bool dispatcherOwnerDestructing = false;

/*!
    \class XcbEventQueue
    \internal

    Lock-free event passing:

    The lock-free solution uses a singly-linked list to pass events from the
    reader thread to the main thread. An atomic operation is used to sync the
    tail node of the list between threads. The reader thread takes special care
    when accessing the tail node. It does not dequeue the last node and does not
    access (read or write) the tail node's 'next' member. This lets the reader
    add more items at the same time as the main thread is dequeuing nodes from
    the head. A custom linked list implementation is used, because QLinkedList
    does not have any thread-safety guarantees and the custom list is more
    lightweight - no reference counting, back links, etc.

    Memory management:

    In a normally functioning application, XCB plugin won't buffer more than few
    batches of events, couple events per batch. Instead of constantly calling
    new / delete, we can create a pool of nodes that we reuse. The main thread
    uses an atomic operation to sync how many nodes have been restored (available
    for reuse). If at some point a user application will block the main thread
    for a long time, we might run out of nodes in the pool. Then we create nodes
    on a heap. These will be automatically "garbage collected" out of the linked
    list, once the main thread stops blocking.
*/

XcbEventQueue::XcbEventQueue(XcbConnection *connection)
    : m_connection(connection)
{
    // When running test cases in auto tests, static variables are preserved
    // between test function runs, even if Q*Application object is destroyed.
    // Reset to default value to account for this.
    dispatcherOwnerDestructing = false;
    qAddPostRoutine([]() {
        QMutexLocker locker(&qAppExiting);
        dispatcherOwnerDestructing = true;
    });

    // Lets init the list with one node, so we don't have to check for
    // this special case in various places.
    m_head = m_flushedTail = qMEventNodeFactory(nullptr);
    m_tail.store(m_head, std::memory_order_release);

    start();
}

XcbEventQueue::~XcbEventQueue()
{
    if (isRunning()) {
        sendCloseConnectionEvent();
        wait();
    }

    flushBufferedEvents();
    while (xcb_generic_event_t *event = takeFirst(QEventLoop::AllEvents))
        free(event);

    if (m_head && m_head->fromHeap)
        delete m_head; // the deferred node

    qCDebug(lcQpaEventReader) << "nodes on heap:" << m_nodesOnHeap;
}

xcb_generic_event_t *XcbEventQueue::takeFirst(QEventLoop::ProcessEventsFlags flags)
{
    // This is the level at which we were moving excluded user input events into
    // separate queue in Qt 4 (see qeventdispatcher_x11.cpp). In this case
    // XcbEventQueue represents Xlib's internal event queue. In Qt 4, Xlib's
    // event queue peeking APIs would not see these events anymore, the same way
    // our peeking functions do not consider m_inputEvents. This design is
    // intentional to keep the same behavior. We could do filtering directly on
    // XcbEventQueue, without the m_inputEvents, but it is not clear if it is
    // needed by anyone who peeks at the native event queue.

    bool excludeUserInputEvents = flags.testFlag(QEventLoop::ExcludeUserInputEvents);
    if (excludeUserInputEvents) {
        xcb_generic_event_t *event = nullptr;
        while ((event = takeFirst())) {
            if (m_connection->isUserInputEvent(event)) {
                m_inputEvents << event;
                continue;
            }
            break;
        }
        return event;
    }

    if (!m_inputEvents.isEmpty())
        return m_inputEvents.takeFirst();
    return takeFirst();
}

xcb_generic_event_t *XcbEventQueue::takeFirst()
{
    if (isEmpty())
        return nullptr;

    xcb_generic_event_t *event = nullptr;
    do {
        event = m_head->event;
        if (m_head == m_flushedTail) {
            // defer dequeuing until next successful flush of events
            if (event) // check if not cleared already by some filter
                m_head->event = nullptr; // if not, clear it
        } else {
            dequeueNode();
            if (!event)
                continue; // consumed by filter or deferred node
        }
    } while (!isEmpty() && !event);

    m_queueModified = m_peekerIndexCacheDirty = true;

    return event;
}

void XcbEventQueue::dequeueNode()
{
    XcbEventNode *node = m_head;
    m_head = m_head->next;
    if (node->fromHeap)
        delete node;
    else
        m_nodesRestored.fetch_add(1, std::memory_order_release);
}

void XcbEventQueue::flushBufferedEvents()
{
    m_flushedTail = m_tail.load(std::memory_order_acquire);
}

XcbEventNode *XcbEventQueue::qMEventNodeFactory(xcb_generic_event_t *event)
{
    static XcbEventNode qMNodePool[PoolSize];

    if (m_freeNodes == 0) // out of nodes, check if the main thread has released any
        m_freeNodes = m_nodesRestored.exchange(0, std::memory_order_acquire);

    if (m_freeNodes) {
        m_freeNodes--;
        if (m_poolIndex == PoolSize) {
            // wrap back to the beginning, we always take and restore nodes in-order
            m_poolIndex = 0;
        }
        XcbEventNode *node = &qMNodePool[m_poolIndex++];
        node->event = event;
        node->next = nullptr;
        return node;
    }

    // the main thread is not flushing events and thus the pool has become empty
    auto node = new XcbEventNode(event);
    node->fromHeap = true;
    qCDebug(lcQpaEventReader) << "[heap] " << m_nodesOnHeap++;
    return node;
}

void XcbEventQueue::run()
{
    xcb_generic_event_t *event = nullptr;
    xcb_connection_t *connection = m_connection->xcb_connection();
    XcbEventNode *tail = m_head;

    auto enqueueEvent = [&tail, this](xcb_generic_event_t *event) {
        if (!isCloseConnectionEvent(event)) {
            tail->next = qMEventNodeFactory(event);
            tail = tail->next;
            m_tail.store(tail, std::memory_order_release);
        } else {
            free(event);
        }
    };

    while (!m_closeConnectionDetected && (event = xcb_wait_for_event(connection))) {
        m_newEventsMutex.lock();
        enqueueEvent(event);
        while (!m_closeConnectionDetected && (event = xcb_poll_for_queued_event(connection)))
            enqueueEvent(event);

        m_newEventsCondition.wakeOne();
        m_newEventsMutex.unlock();
        wakeUpDispatcher();
    }

    if (!m_closeConnectionDetected) {
        // Connection was terminated not by us. Wake up dispatcher, which will
        // call processMEvents(), where we handle the connection errors via
        // xcb_connection_has_error().
        wakeUpDispatcher();
    }
}

void XcbEventQueue::wakeUpDispatcher()
{
    QMutexLocker locker(&qAppExiting);
    if (!dispatcherOwnerDestructing) {
        // This thread can run before a dispatcher has been created,
        // so check if it is ready.
        if (QCoreApplication::eventDispatcher())
            QCoreApplication::eventDispatcher()->wakeUp();
    }
}

qint32 XcbEventQueue::generatePeekerId()
{
    const qint32 peekerId = m_peekerIdSource++;
    m_peekerToNode.insert(peekerId, nullptr);
    return peekerId;
}

bool XcbEventQueue::removePeekerId(qint32 peekerId)
{
    const auto it = m_peekerToNode.constFind(peekerId);
    if (it == m_peekerToNode.constEnd()) {
        qCWarning(lcQpaXcb, "failed to remove unknown peeker id: %d", peekerId);
        return false;
    }
    m_peekerToNode.erase(it);
    if (m_peekerToNode.isEmpty()) {
        m_peekerIdSource = 0; // Once the hash becomes empty, we can start reusing IDs
        m_peekerIndexCacheDirty = false;
    }
    return true;
}

bool XcbEventQueue::peekEventQueue(PeekerCallback peeker, void *peekerData,
                                    PeekOptions option, qint32 peekerId)
{
    const bool peekerIdProvided = peekerId != -1;
    auto peekerToNodeIt = m_peekerToNode.find(peekerId);

    if (peekerIdProvided && peekerToNodeIt == m_peekerToNode.end()) {
        qCWarning(lcQpaXcb, "failed to find index for unknown peeker id: %d", peekerId);
        return false;
    }

    const bool useCache = option.testFlag(PeekOption::PeekFromCachedIndex);
    if (useCache && !peekerIdProvided) {
        qCWarning(lcQpaXcb, "PeekOption::PeekFromCachedIndex requires peeker id");
        return false;
    }

    if (peekerIdProvided && m_peekerIndexCacheDirty) {
        for (auto &node : m_peekerToNode) // reset cache
            node = nullptr;
        m_peekerIndexCacheDirty = false;
    }

    flushBufferedEvents();
    if (isEmpty())
        return false;

    const auto startNode = [this, useCache, peekerToNodeIt]() -> XcbEventNode * {
        if (useCache) {
            const XcbEventNode *cachedNode = peekerToNodeIt.value();
            if (!cachedNode)
                return m_head; // cache was reset
            if (cachedNode == m_flushedTail)
                return nullptr; // no new events since the last call
            return cachedNode->next;
        }
        return m_head;
    }();

    if (!startNode)
        return false;

    // A peeker may call QCoreApplication::processEvents(), which will cause
    // XcbConnection::processMEvents() to modify the queue we are currently
    // looping through;
    m_queueModified = false;
    bool result = false;

    XcbEventNode *node = startNode;
    do {
        xcb_generic_event_t *event = node->event;
        if (event && peeker(event, peekerData)) {
            result = true;
            break;
        }
        if (node == m_flushedTail)
            break;
        node = node->next;
    } while (!m_queueModified);

    // Update the cached index if the queue was not modified, and hence the
    // cache is still valid.
    if (peekerIdProvided && node != startNode && !m_queueModified) {
        // Before updating, make sure that a peeker callback did not remove
        // the peeker id.
        peekerToNodeIt = m_peekerToNode.find(peekerId);
        if (peekerToNodeIt != m_peekerToNode.end())
            *peekerToNodeIt = node; // id still in the cache, update node
    }

    return result;
}

void XcbEventQueue::waitForNewEvents(unsigned long time)
{
    QMutexLocker locker(&m_newEventsMutex);
    XcbEventNode *tailBeforeFlush = m_flushedTail;
    flushBufferedEvents();
    if (tailBeforeFlush != m_flushedTail)
        return;
    m_newEventsCondition.wait(&m_newEventsMutex, time);
}

void XcbEventQueue::sendCloseConnectionEvent() const
{
    // A hack to close XCB connection. Apparently XCB does not have any APIs for this?
    xcb_client_message_event_t event;
    memset(&event, 0, sizeof(event));

    xcb_connection_t *c = m_connection->xcb_connection();
    const xcb_window_t window = xcb_generate_id(c);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(m_connection->setup());
    xcb_screen_t *screen = it.data;
    xcb_create_window(c, XCB_COPY_FROM_PARENT,
                      window, screen->root,
                      0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                      screen->root_visual, 0, nullptr);

    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.sequence = 0;
    event.window = window;
    event.type = m_connection->atom(XcbAtom::_QT_CLOSE_CONNECTION);
    event.data.data32[0] = 0;

    xcb_send_event(c, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
    xcb_destroy_window(c, window);
    xcb_flush(c);
}

bool XcbEventQueue::isCloseConnectionEvent(const xcb_generic_event_t *event)
{
    if (event && (event->response_type & ~0x80) == XCB_CLIENT_MESSAGE) {
        auto clientMessage = reinterpret_cast<const xcb_client_message_event_t *>(event);
        if (clientMessage->type == m_connection->atom(XcbAtom::_QT_CLOSE_CONNECTION))
            m_closeConnectionDetected = true;
    }
    return m_closeConnectionDetected;
}
