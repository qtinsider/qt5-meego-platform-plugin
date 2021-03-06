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

#include "mplatformclipboard.h"

#include "xcbconnection.h"
#include "mplatformscreen.h"
#include "xcbmime.h"
#include "mplatformwindow.h"

#include <private/qguiapplication_p.h>
#include <QElapsedTimer>

#include <QtCore/QDebug>

class MPlatformClipboardMime : public XcbMime
{
    Q_OBJECT
public:
    MPlatformClipboardMime(QClipboard::Mode mode, MPlatformClipboard *clipboard)
        : XcbMime()
        , m_clipboard(clipboard)
    {
        switch (mode) {
        case QClipboard::Selection:
            modeAtom = XCB_ATOM_PRIMARY;
            break;

        case QClipboard::Clipboard:
            modeAtom = m_clipboard->atom(XcbAtom::CLIPBOARD);
            break;

        default:
            qWarning("MPlatformClipboardMime: Internal error: Unsupported clipboard mode");
            break;
        }
    }

    void reset()
    {
        formatList.clear();
    }

    bool isEmpty() const
    {
        return m_clipboard->getSelectionOwner(modeAtom) == XCB_NONE;
    }

protected:
    QStringList formats_sys() const override
    {
        if (isEmpty())
            return QStringList();

        if (!formatList.count()) {
            MPlatformClipboardMime *that = const_cast<MPlatformClipboardMime *>(this);
            // get the list of targets from the current clipboard owner - we do this
            // once so that multiple calls to this function don't require multiple
            // server round trips...
            that->format_atoms = m_clipboard->getDataInFormat(modeAtom, m_clipboard->atom(XcbAtom::TARGETS));

            if (format_atoms.size() > 0) {
                const xcb_atom_t *targets = (const xcb_atom_t *) format_atoms.data();
                int size = format_atoms.size() / sizeof(xcb_atom_t);

                for (int i = 0; i < size; ++i) {
                    if (targets[i] == 0)
                        continue;

                    QString format = mimeAtomToString(m_clipboard->connection(), targets[i]);
                    if (!formatList.contains(format))
                        that->formatList.append(format);
                }
            }
        }

        return formatList;
    }

    bool hasFormat_sys(const QString &format) const override
    {
        QStringList list = formats();
        return list.contains(format);
    }

    QVariant retrieveData_sys(const QString &fmt, QVariant::Type type) const override
    {
        auto requestedType = QMetaType::Type(type);
        if (fmt.isEmpty() || isEmpty())
            return QByteArray();

        (void)formats(); // trigger update of format list

        QVector<xcb_atom_t> atoms;
        const xcb_atom_t *targets = (const xcb_atom_t *) format_atoms.data();
        int size = format_atoms.size() / sizeof(xcb_atom_t);
        atoms.reserve(size);
        for (int i = 0; i < size; ++i)
            atoms.append(targets[i]);

        QByteArray encoding;
        xcb_atom_t fmtatom = mimeAtomForFormat(m_clipboard->connection(), fmt, requestedType, atoms, &encoding);

        if (fmtatom == 0)
            return QVariant();

        return mimeConvertToFormat(m_clipboard->connection(), fmtatom, m_clipboard->getDataInFormat(modeAtom, fmtatom), fmt, requestedType, encoding);
    }
private:

    xcb_atom_t modeAtom;
    MPlatformClipboard *m_clipboard;
    QStringList formatList;
    QByteArray format_atoms;
};

XcbClipboardTransaction::XcbClipboardTransaction(MPlatformClipboard *clipboard, xcb_window_t w,
                                               xcb_atom_t p, QByteArray d, xcb_atom_t t, int f)
    : m_clipboard(clipboard), m_window(w), m_property(p), m_data(d), m_target(t), m_format(f)
{
    const quint32 values[] = { XCB_EVENT_MASK_PROPERTY_CHANGE };
    xcb_change_window_attributes(m_clipboard->xcb_connection(), m_window,
                                 XCB_CW_EVENT_MASK, values);

    m_abortTimerId = startTimer(m_clipboard->clipboardTimeout());
}

XcbClipboardTransaction::~XcbClipboardTransaction()
{
    if (m_abortTimerId)
        killTimer(m_abortTimerId);
    m_abortTimerId = 0;
    m_clipboard->removeTransaction(m_window);
}

bool XcbClipboardTransaction::updateIncrementalProperty(const xcb_property_notify_event_t *event)
{
    if (event->atom != m_property || event->state != XCB_PROPERTY_DELETE)
        return false;

    // restart the timer
    if (m_abortTimerId)
        killTimer(m_abortTimerId);
    m_abortTimerId = startTimer(m_clipboard->clipboardTimeout());

    uint bytes_left = uint(m_data.size()) - m_offset;
    if (bytes_left > 0) {
        int increment = m_clipboard->increment();
        uint bytes_to_send = qMin(uint(increment), bytes_left);

        qCDebug(lcQpaClipboard, "sending %d bytes, %d remaining, transaction: %p)",
                bytes_to_send, bytes_left - bytes_to_send, this);

        uint32_t dataSize = bytes_to_send / (m_format / 8);
        xcb_change_property(m_clipboard->xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                            m_property, m_target, m_format, dataSize, m_data.constData() + m_offset);
        m_offset += bytes_to_send;
    } else {
        qCDebug(lcQpaClipboard, "transaction %p completed", this);

        xcb_change_property(m_clipboard->xcb_connection(), XCB_PROP_MODE_REPLACE, m_window,
                            m_property, m_target, m_format, 0, nullptr);

        const quint32 values[] = { XCB_EVENT_MASK_NO_EVENT };
        xcb_change_window_attributes(m_clipboard->xcb_connection(), m_window,
                                     XCB_CW_EVENT_MASK, values);
        delete this; // self destroy
    }
    return true;
}


void XcbClipboardTransaction::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == m_abortTimerId) {
        // this can happen when the X client we are sending data
        // to decides to exit (normally or abnormally)
        qCDebug(lcQpaClipboard, "timed out while sending data to %p", this);
        delete this; // self destroy
    }
}

const int MPlatformClipboard::clipboard_timeout = 5000;

MPlatformClipboard::MPlatformClipboard(XcbConnection *c)
    : XcbObject(c), QPlatformClipboard()
{
    Q_ASSERT(QClipboard::Clipboard == 0);
    Q_ASSERT(QClipboard::Selection == 1);
    m_clientClipboard[QClipboard::Clipboard] = nullptr;
    m_clientClipboard[QClipboard::Selection] = nullptr;
    m_timestamp[QClipboard::Clipboard] = XCB_CURRENT_TIME;
    m_timestamp[QClipboard::Selection] = XCB_CURRENT_TIME;
    m_owner = connection()->getQtSelectionOwner();

    if (connection()->hasXFixes()) {
        const uint32_t mask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
        xcb_xfixes_select_selection_input_checked(xcb_connection(), m_owner, XCB_ATOM_PRIMARY, mask);
        xcb_xfixes_select_selection_input_checked(xcb_connection(), m_owner, atom(XcbAtom::CLIPBOARD), mask);
    }

    // xcb_change_property_request_t and xcb_get_property_request_t are the same size
    m_maxPropertyRequestDataBytes = connection()->maxRequestDataBytes(sizeof(xcb_change_property_request_t));
}

MPlatformClipboard::~MPlatformClipboard()
{
    m_clipboard_closing = true;
    // Transfer the clipboard content to the clipboard manager if we own a selection
    if (m_timestamp[QClipboard::Clipboard] != XCB_CURRENT_TIME ||
            m_timestamp[QClipboard::Selection] != XCB_CURRENT_TIME) {

        // First we check if there is a clipboard manager.
        auto reply = Q_XCB_REPLY(xcb_get_selection_owner, xcb_connection(), atom(XcbAtom::CLIPBOARD_MANAGER));
        if (reply && reply->owner != XCB_NONE) {
            // we delete the property so the manager saves all TARGETS.
            xcb_delete_property(xcb_connection(), m_owner, atom(XcbAtom::_QT_SELECTION));
            xcb_convert_selection(xcb_connection(), m_owner, atom(XcbAtom::CLIPBOARD_MANAGER), atom(XcbAtom::SAVE_TARGETS),
                                  atom(XcbAtom::_QT_SELECTION), connection()->time());
            connection()->sync();

            // waiting until the clipboard manager fetches the content.
            if (auto event = waitForClipboardEvent(m_owner, XCB_SELECTION_NOTIFY, true)) {
                free(event);
            } else {
                qWarning("MPlatformClipboard: Unable to receive an event from the "
                         "clipboard manager in a reasonable time");
            }
        }
    }

    if (m_clientClipboard[QClipboard::Clipboard] != m_clientClipboard[QClipboard::Selection])
        delete m_clientClipboard[QClipboard::Clipboard];
    delete m_clientClipboard[QClipboard::Selection];
}

bool MPlatformClipboard::handlePropertyNotify(const xcb_generic_event_t *event)
{
    if (m_transactions.isEmpty() || event->response_type != XCB_PROPERTY_NOTIFY)
        return false;

    auto propertyNotify = reinterpret_cast<const xcb_property_notify_event_t *>(event);
    TransactionMap::Iterator it = m_transactions.find(propertyNotify->window);
    if (it == m_transactions.constEnd())
        return false;

    return (*it)->updateIncrementalProperty(propertyNotify);
}

xcb_window_t MPlatformClipboard::getSelectionOwner(xcb_atom_t atom) const
{
    return connection()->getSelectionOwner(atom);
}

xcb_atom_t MPlatformClipboard::atomForMode(QClipboard::Mode mode) const
{
    if (mode == QClipboard::Clipboard)
        return atom(XcbAtom::CLIPBOARD);
    if (mode == QClipboard::Selection)
        return XCB_ATOM_PRIMARY;
    return XCB_NONE;
}

QClipboard::Mode MPlatformClipboard::modeForAtom(xcb_atom_t a) const
{
    if (a == XCB_ATOM_PRIMARY)
        return QClipboard::Selection;
    if (a == atom(XcbAtom::CLIPBOARD))
        return QClipboard::Clipboard;
    // not supported enum value, used to detect errors
    return QClipboard::FindBuffer;
}


QMimeData * MPlatformClipboard::mimeData(QClipboard::Mode mode)
{
    if (mode > QClipboard::Selection)
        return nullptr;

    xcb_window_t clipboardOwner = getSelectionOwner(atomForMode(mode));
    if (clipboardOwner == owner()) {
        return m_clientClipboard[mode];
    } else {
        if (!m_xClipboard[mode])
            m_xClipboard[mode].reset(new MPlatformClipboardMime(mode, this));

        return m_xClipboard[mode].data();
    }
}

void MPlatformClipboard::setMimeData(QMimeData *data, QClipboard::Mode mode)
{
    if (mode > QClipboard::Selection)
        return;

    MPlatformClipboardMime *xClipboard = nullptr;
    // verify if there is data to be cleared on global X Clipboard.
    if (!data) {
        xClipboard = qobject_cast<MPlatformClipboardMime *>(mimeData(mode));
        if (xClipboard) {
            if (xClipboard->isEmpty())
                return;
        }
    }

    if (!xClipboard && (m_clientClipboard[mode] == data))
        return;

    xcb_atom_t modeAtom = atomForMode(mode);
    xcb_window_t newOwner = XCB_NONE;

    if (m_clientClipboard[mode]) {
        if (m_clientClipboard[QClipboard::Clipboard] != m_clientClipboard[QClipboard::Selection])
            delete m_clientClipboard[mode];
        m_clientClipboard[mode] = nullptr;
        m_timestamp[mode] = XCB_CURRENT_TIME;
    }

    if (connection()->time() == XCB_CURRENT_TIME)
        connection()->setTime(connection()->getTimestamp());

    if (data) {
        newOwner = owner();

        m_clientClipboard[mode] = data;
        m_timestamp[mode] = connection()->time();
    }

    xcb_set_selection_owner(xcb_connection(), newOwner, modeAtom, connection()->time());

    if (getSelectionOwner(modeAtom) != newOwner) {
        qWarning("MPlatformClipboard::setMimeData: Cannot set X11 selection owner");
    }

    emitChanged(mode);
}

bool MPlatformClipboard::supportsMode(QClipboard::Mode mode) const
{
    if (mode <= QClipboard::Selection)
        return true;
    return false;
}

bool MPlatformClipboard::ownsMode(QClipboard::Mode mode) const
{
    if (m_owner == XCB_NONE || mode > QClipboard::Selection)
        return false;

    Q_ASSERT(m_timestamp[mode] == XCB_CURRENT_TIME || getSelectionOwner(atomForMode(mode)) == m_owner);

    return m_timestamp[mode] != XCB_CURRENT_TIME;
}

MPlatformScreen *MPlatformClipboard::screen() const
{
    return connection()->primaryScreen();
}

xcb_window_t MPlatformClipboard::requestor() const
{
     MPlatformScreen *platformScreen = screen();

    if (!m_requestor && platformScreen) {
        const int x = 0, y = 0, w = 3, h = 3;
        MPlatformClipboard *that = const_cast<MPlatformClipboard *>(this);

        xcb_window_t window = xcb_generate_id(xcb_connection());
        xcb_create_window(xcb_connection(),
                          XCB_COPY_FROM_PARENT,                  // depth -- same as root
                          window,                                // window id
                          platformScreen->screen()->root,        // parent window id
                          x, y, w, h,
                          0,                                     // border width
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,         // window class
                          platformScreen->screen()->root_visual, // visual
                          0,                                     // value mask
                          nullptr);                                    // value list

        uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        xcb_change_window_attributes(xcb_connection(), window, XCB_CW_EVENT_MASK, &mask);

        that->setRequestor(window);
    }
    return m_requestor;
}

void MPlatformClipboard::setRequestor(xcb_window_t window)
{
    if (m_requestor != XCB_NONE) {
        xcb_destroy_window(xcb_connection(), m_requestor);
    }
    m_requestor = window;
}

xcb_window_t MPlatformClipboard::owner() const
{
    return m_owner;
}

xcb_atom_t MPlatformClipboard::sendTargetsSelection(QMimeData *d, xcb_window_t window, xcb_atom_t property)
{
    QVector<xcb_atom_t> types;
    QStringList formats = QInternalMimeData::formatsHelper(d);
    for (int i = 0; i < formats.size(); ++i) {
        QVector<xcb_atom_t> atoms = XcbMime::mimeAtomsForFormat(connection(), formats.at(i));
        for (int j = 0; j < atoms.size(); ++j) {
            if (!types.contains(atoms.at(j)))
                types.append(atoms.at(j));
        }
    }
    types.append(atom(XcbAtom::TARGETS));
    types.append(atom(XcbAtom::MULTIPLE));
    types.append(atom(XcbAtom::TIMESTAMP));
    types.append(atom(XcbAtom::SAVE_TARGETS));

    xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property, XCB_ATOM_ATOM,
                        32, types.size(), (const void *)types.constData());
    return property;
}

xcb_atom_t MPlatformClipboard::sendSelection(QMimeData *d, xcb_atom_t target, xcb_window_t window, xcb_atom_t property)
{
    xcb_atom_t atomFormat = target;
    int dataFormat = 0;
    QByteArray data;

    QString fmt = XcbMime::mimeAtomToString(connection(), target);
    if (fmt.isEmpty()) { // Not a MIME type we have
//        qDebug() << "QClipboard: send_selection(): converting to type" << connection()->atomName(target) << "is not supported";
        return XCB_NONE;
    }
//    qDebug() << "QClipboard: send_selection(): converting to type" << fmt;

    if (XcbMime::mimeDataForAtom(connection(), target, d, &data, &atomFormat, &dataFormat)) {

         // don't allow INCR transfers when using MULTIPLE or to
        // Motif clients (since Motif doesn't support INCR)
        static xcb_atom_t motif_clip_temporary = atom(XcbAtom::CLIP_TEMPORARY);
        bool allow_incr = property != motif_clip_temporary;
        // This 'bool' can be removed once there is a proper fix for QTBUG-32853
        if (m_clipboard_closing)
            allow_incr = false;

        if (data.size() > m_maxPropertyRequestDataBytes && allow_incr) {
            long bytes = data.size();
            xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property,
                                atom(XcbAtom::INCR), 32, 1, (const void *)&bytes);
            auto transaction = new XcbClipboardTransaction(this, window, property, data, atomFormat, dataFormat);
            m_transactions.insert(window, transaction);
            return property;
        }

        // make sure we can perform the XChangeProperty in a single request
        if (data.size() > m_maxPropertyRequestDataBytes)
            return XCB_NONE; // ### perhaps use several XChangeProperty calls w/ PropModeAppend?
        int dataSize = data.size() / (dataFormat / 8);
        // use a single request to transfer data
        xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, window, property, atomFormat,
                            dataFormat, dataSize, (const void *)data.constData());
    }
    return property;
}

void MPlatformClipboard::handleSelectionClearRequest(xcb_selection_clear_event_t *event)
{
    QClipboard::Mode mode = modeForAtom(event->selection);
    if (mode > QClipboard::Selection)
        return;

    // ignore the event if it was generated before we gained selection ownership
    if (m_timestamp[mode] != XCB_CURRENT_TIME && event->time <= m_timestamp[mode])
        return;

//    DEBUG("QClipboard: new selection owner 0x%lx at time %lx (ours %lx)",
//          XGetSelectionOwner(dpy, XA_PRIMARY),
//          xevent->xselectionclear.time, d->timestamp);

    xcb_window_t newOwner = getSelectionOwner(event->selection);

    /* If selection ownership was given up voluntarily from QClipboard::clear(), then we do nothing here
    since its already handled in setMimeData. Otherwise, the event must have come from another client
    as a result of a call to xcb_set_selection_owner in which case we need to delete the local mime data
    */
    if (newOwner != XCB_NONE) {
        if (m_clientClipboard[QClipboard::Clipboard] != m_clientClipboard[QClipboard::Selection])
            delete m_clientClipboard[mode];
        m_clientClipboard[mode] = nullptr;
        m_timestamp[mode] = XCB_CURRENT_TIME;
    }
}

void MPlatformClipboard::handleSelectionRequest(xcb_selection_request_event_t *req)
{
    if (requestor() && req->requestor == requestor()) {
        qWarning("MPlatformClipboard: Selection request should be caught before");
        return;
    }

    q_padded_xcb_event<xcb_selection_notify_event_t> event = {};
    event.response_type = XCB_SELECTION_NOTIFY;
    event.requestor = req->requestor;
    event.selection = req->selection;
    event.target    = req->target;
    event.property  = XCB_NONE;
    event.time      = req->time;

    QMimeData *d;
    QClipboard::Mode mode = modeForAtom(req->selection);
    if (mode > QClipboard::Selection) {
        qWarning() << "MPlatformClipboard: Unknown selection" << connection()->atomName(req->selection);
        xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }

    d = m_clientClipboard[mode];

    if (!d) {
        qWarning("MPlatformClipboard: Cannot transfer data, no data available");
        xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }

    if (m_timestamp[mode] == XCB_CURRENT_TIME // we don't own the selection anymore
            || (req->time != XCB_CURRENT_TIME && req->time < m_timestamp[mode])) {
        qWarning("MPlatformClipboard: SelectionRequest too old");
        xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
        return;
    }

    xcb_atom_t targetsAtom = atom(XcbAtom::TARGETS);
    xcb_atom_t multipleAtom = atom(XcbAtom::MULTIPLE);
    xcb_atom_t timestampAtom = atom(XcbAtom::TIMESTAMP);

    struct AtomPair { xcb_atom_t target; xcb_atom_t property; } *multi = nullptr;
    xcb_atom_t multi_type = XCB_NONE;
    int multi_format = 0;
    int nmulti = 0;
    int imulti = -1;
    bool multi_writeback = false;

    if (req->target == multipleAtom) {
        QByteArray multi_data;
        if (req->property == XCB_NONE
            || !clipboardReadProperty(req->requestor, req->property, false, &multi_data,
                                           nullptr, &multi_type, &multi_format)
            || multi_format != 32) {
            // MULTIPLE property not formatted correctly
            xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
            return;
        }
        nmulti = multi_data.size()/sizeof(*multi);
        multi = new AtomPair[nmulti];
        memcpy(multi,multi_data.data(),multi_data.size());
        imulti = 0;
    }

    for (; imulti < nmulti; ++imulti) {
        xcb_atom_t target;
        xcb_atom_t property;

        if (multi) {
            target = multi[imulti].target;
            property = multi[imulti].property;
        } else {
            target = req->target;
            property = req->property;
            if (property == XCB_NONE) // obsolete client
                property = target;
        }

        xcb_atom_t ret = XCB_NONE;
        if (target == XCB_NONE || property == XCB_NONE) {
            ;
        } else if (target == timestampAtom) {
            if (m_timestamp[mode] != XCB_CURRENT_TIME) {
                xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, req->requestor,
                                    property, XCB_ATOM_INTEGER, 32, 1, &m_timestamp[mode]);
                ret = property;
            } else {
                qWarning("MPlatformClipboard: Invalid data timestamp");
            }
        } else if (target == targetsAtom) {
            ret = sendTargetsSelection(d, req->requestor, property);
        } else {
            ret = sendSelection(d, target, req->requestor, property);
        }

        if (nmulti > 0) {
            if (ret == XCB_NONE) {
                multi[imulti].property = XCB_NONE;
                multi_writeback = true;
            }
        } else {
            event.property = ret;
            break;
        }
    }

    if (nmulti > 0) {
        if (multi_writeback) {
            // according to ICCCM 2.6.2 says to put None back
            // into the original property on the requestor window
            xcb_change_property(xcb_connection(), XCB_PROP_MODE_REPLACE, req->requestor, req->property,
                                multi_type, 32, nmulti*2, (const void *)multi);
        }

        delete [] multi;
        event.property = req->property;
    }

    // send selection notify to requestor
    xcb_send_event(xcb_connection(), false, req->requestor, XCB_EVENT_MASK_NO_EVENT, (const char *)&event);
}

void MPlatformClipboard::handleXFixesSelectionRequest(xcb_xfixes_selection_notify_event_t *event)
{
    QClipboard::Mode mode = modeForAtom(event->selection);
    if (mode > QClipboard::Selection)
        return;

    // Note1: Here we care only about the xfixes events that come from other processes.
    // Note2: If the QClipboard::clear() is issued, event->owner is XCB_NONE,
    // so we check selection_timestamp to not handle our own QClipboard::clear().
    if (event->owner != owner() && event->selection_timestamp > m_timestamp[mode]) {
        if (!m_xClipboard[mode]) {
            m_xClipboard[mode].reset(new MPlatformClipboardMime(mode, this));
        } else {
            m_xClipboard[mode]->reset();
        }
        emitChanged(mode);
    } else if (event->subtype == XCB_XFIXES_SELECTION_EVENT_SELECTION_CLIENT_CLOSE ||
               event->subtype == XCB_XFIXES_SELECTION_EVENT_SELECTION_WINDOW_DESTROY)
        emitChanged(mode);
}

bool MPlatformClipboard::clipboardReadProperty(xcb_window_t win, xcb_atom_t property, bool deleteProperty, QByteArray *buffer, int *size, xcb_atom_t *type, int *format)
{
    xcb_atom_t   dummy_type;
    int    dummy_format;

    if (!type)                                // allow null args
        type = &dummy_type;
    if (!format)
        format = &dummy_format;

    // Don't read anything, just get the size of the property data
    auto reply = Q_XCB_REPLY(xcb_get_property, xcb_connection(), false, win, property, XCB_GET_PROPERTY_TYPE_ANY, 0, 0);
    if (!reply || reply->type == XCB_NONE) {
        buffer->resize(0);
        return false;
    }
    *type = reply->type;
    *format = reply->format;

    auto bytes_left = reply->bytes_after;

    int  offset = 0, buffer_offset = 0;

    int newSize = bytes_left;
    buffer->resize(newSize);

    bool ok = (buffer->size() == newSize);

    if (ok && newSize) {
        // could allocate buffer

        while (bytes_left) {
            // more to read...

            reply = Q_XCB_REPLY(xcb_get_property, xcb_connection(), false, win, property,
                                XCB_GET_PROPERTY_TYPE_ANY, offset, m_maxPropertyRequestDataBytes / 4);
            if (!reply || reply->type == XCB_NONE)
                break;

            *type = reply->type;
            *format = reply->format;
            bytes_left = reply->bytes_after;
            char *data = (char *)xcb_get_property_value(reply.get());
            int length = xcb_get_property_value_length(reply.get());

            // Here we check if we get a buffer overflow and tries to
            // recover -- this shouldn't normally happen, but it doesn't
            // hurt to be defensive
            if ((int)(buffer_offset + length) > buffer->size()) {
                qWarning("MPlatformClipboard: buffer overflow");
                length = buffer->size() - buffer_offset;

                // escape loop
                bytes_left = 0;
            }

            memcpy(buffer->data() + buffer_offset, data, length);
            buffer_offset += length;

            if (bytes_left) {
                // offset is specified in 32-bit multiples
                offset += length / 4;
            }
        }
    }


    // correct size, not 0-term.
    if (size)
        *size = buffer_offset;
    if (*type == atom(XcbAtom::INCR))
        m_incr_receive_time = connection()->getTimestamp();
    if (deleteProperty)
        xcb_delete_property(xcb_connection(), win, property);

    connection()->flush();

    return ok;
}

xcb_generic_event_t *MPlatformClipboard::waitForClipboardEvent(xcb_window_t window, int type, bool checkManager)
{
    QElapsedTimer timer;
    timer.start();
    XcbEventQueue *queue = connection()->eventQueue();
    do {
        auto e = queue->peek([window, type](xcb_generic_event_t *event, int eventType) {
            if (eventType != type)
                return false;
            if (eventType == XCB_PROPERTY_NOTIFY) {
                auto propertyNotify = reinterpret_cast<xcb_property_notify_event_t *>(event);
                return propertyNotify->window == window;
            }
            if (eventType == XCB_SELECTION_NOTIFY) {
                auto selectionNotify = reinterpret_cast<xcb_selection_notify_event_t *>(event);
                return selectionNotify->requestor == window;
            }
            return false;
        });
        if (e) // found the waited for event
            return e;

        if (checkManager) {
            auto reply = Q_XCB_REPLY(xcb_get_selection_owner, xcb_connection(), atom(XcbAtom::CLIPBOARD_MANAGER));
            if (!reply || reply->owner == XCB_NONE)
                return nullptr;
        }

        // process other clipboard events, since someone is probably requesting data from us
        auto clipboardAtom = atom(XcbAtom::CLIPBOARD);
        e = queue->peek([clipboardAtom](xcb_generic_event_t *event, int type) {
            xcb_atom_t selection = XCB_ATOM_NONE;
            if (type == XCB_SELECTION_REQUEST)
                selection = reinterpret_cast<xcb_selection_request_event_t *>(event)->selection;
            else if (type == XCB_SELECTION_CLEAR)
                selection = reinterpret_cast<xcb_selection_clear_event_t *>(event)->selection;
            return selection == XCB_ATOM_PRIMARY || selection == clipboardAtom;
        });
        if (e) {
            connection()->handleXcbEvent(e);
            free(e);
        }

        connection()->flush();

        const auto elapsed = timer.elapsed();
        if (elapsed < clipboard_timeout)
            queue->waitForNewEvents(clipboard_timeout - elapsed);
    } while (timer.elapsed() < clipboard_timeout);

    return nullptr;
}

QByteArray MPlatformClipboard::clipboardReadIncrementalProperty(xcb_window_t win, xcb_atom_t property, int nbytes, bool nullterm)
{
    QByteArray buf;
    QByteArray tmp_buf;
    bool alloc_error = false;
    int  length;
    int  offset = 0;
    xcb_timestamp_t prev_time = m_incr_receive_time;

    if (nbytes > 0) {
        // Reserve buffer + zero-terminator (for text data)
        // We want to complete the INCR transfer even if we cannot
        // allocate more memory
        buf.resize(nbytes+1);
        alloc_error = buf.size() != nbytes+1;
    }

    for (;;) {
        connection()->flush();
        xcb_generic_event_t *ge = waitForClipboardEvent(win, XCB_PROPERTY_NOTIFY);
        if (!ge)
            break;
        xcb_property_notify_event_t *event = (xcb_property_notify_event_t *)ge;
        QScopedPointer<xcb_property_notify_event_t, QScopedPointerPodDeleter> guard(event);

        if (event->atom != property
                || event->state != XCB_PROPERTY_NEW_VALUE
                    || event->time < prev_time)
            continue;
        prev_time = event->time;

        if (clipboardReadProperty(win, property, true, &tmp_buf, &length, nullptr, nullptr)) {
            if (length == 0) {                // no more data, we're done
                if (nullterm) {
                    buf.resize(offset+1);
                    buf[offset] = '\0';
                } else {
                    buf.resize(offset);
                }
                return buf;
            } else if (!alloc_error) {
                if (offset+length > (int)buf.size()) {
                    buf.resize(offset+length+65535);
                    if (buf.size() != offset+length+65535) {
                        alloc_error = true;
                        length = buf.size() - offset;
                    }
                }
                memcpy(buf.data()+offset, tmp_buf.constData(), length);
                tmp_buf.resize(0);
                offset += length;
            }
        } else {
            break;
        }
    }

    // timed out ... create a new requestor window, otherwise the requestor
    // could consider next request to be still part of this timed out request
    setRequestor(0);

    return QByteArray();
}

QByteArray MPlatformClipboard::getDataInFormat(xcb_atom_t modeAtom, xcb_atom_t fmtAtom)
{
    return getSelection(modeAtom, fmtAtom, atom(XcbAtom::_QT_SELECTION));
}

QByteArray MPlatformClipboard::getSelection(xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t time)
{
    QByteArray buf;
    xcb_window_t win = requestor();

    if (time == 0) time = connection()->time();

    xcb_delete_property(xcb_connection(), win, property);
    xcb_convert_selection(xcb_connection(), win, selection, target, property, time);

    connection()->sync();

    xcb_generic_event_t *ge = waitForClipboardEvent(win, XCB_SELECTION_NOTIFY);
    bool no_selection = !ge || ((xcb_selection_notify_event_t *)ge)->property == XCB_NONE;
    free(ge);

    if (no_selection)
        return buf;

    xcb_atom_t type;
    if (clipboardReadProperty(win, property, true, &buf, nullptr, &type, nullptr)) {
        if (type == atom(XcbAtom::INCR)) {
            int nbytes = buf.size() >= 4 ? *((int*)buf.data()) : 0;
            buf = clipboardReadIncrementalProperty(win, property, nbytes, false);
        }
    }

    return buf;
}

#include "mplatformclipboard.moc"
