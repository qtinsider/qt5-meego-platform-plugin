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

#ifndef MPLATFORMCLIPBOARD_H
#define MPLATFORMCLIPBOARD_H

#include <qpa/qplatformclipboard.h>
#include <xcbobject.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <QtCore/QObject>

class XcbConnection;
class MPlatformScreen;
class MPlatformClipboard;
class MPlatformClipboardMime;

class XcbClipboardTransaction : public QObject
{
    Q_OBJECT
public:
    XcbClipboardTransaction(MPlatformClipboard *clipboard, xcb_window_t w, xcb_atom_t p,
                           QByteArray d, xcb_atom_t t, int f);
    ~XcbClipboardTransaction();

    bool updateIncrementalProperty(const xcb_property_notify_event_t *event);

protected:
    void timerEvent(QTimerEvent *ev) override;

private:
    MPlatformClipboard *m_clipboard;
    xcb_window_t m_window;
    xcb_atom_t m_property;
    QByteArray m_data;
    xcb_atom_t m_target;
    uint8_t m_format;
    uint m_offset = 0;
    int m_abortTimerId = 0;
};

class MPlatformClipboard : public XcbObject, public QPlatformClipboard
{
public:
    MPlatformClipboard(XcbConnection *connection);
    ~MPlatformClipboard();

    QMimeData *mimeData(QClipboard::Mode mode) override;
    void setMimeData(QMimeData *data, QClipboard::Mode mode) override;

    bool supportsMode(QClipboard::Mode mode) const override;
    bool ownsMode(QClipboard::Mode mode) const override;

    MPlatformScreen *screen() const;

    xcb_window_t requestor() const;
    void setRequestor(xcb_window_t window);

    xcb_window_t owner() const;

    void handleSelectionRequest(xcb_selection_request_event_t *event);
    void handleSelectionClearRequest(xcb_selection_clear_event_t *event);
    void handleXFixesSelectionRequest(xcb_xfixes_selection_notify_event_t *event);

    bool clipboardReadProperty(xcb_window_t win, xcb_atom_t property, bool deleteProperty, QByteArray *buffer, int *size, xcb_atom_t *type, int *format);
    QByteArray clipboardReadIncrementalProperty(xcb_window_t win, xcb_atom_t property, int nbytes, bool nullterm);

    QByteArray getDataInFormat(xcb_atom_t modeAtom, xcb_atom_t fmtatom);

    bool handlePropertyNotify(const xcb_generic_event_t *event);

    xcb_window_t getSelectionOwner(xcb_atom_t atom) const;
    QByteArray getSelection(xcb_atom_t selection, xcb_atom_t target, xcb_atom_t property, xcb_timestamp_t t = 0);

    int increment() const { return m_maxPropertyRequestDataBytes; }
    int clipboardTimeout() const { return clipboard_timeout; }

    void removeTransaction(xcb_window_t window) { m_transactions.remove(window); }

private:
    xcb_generic_event_t *waitForClipboardEvent(xcb_window_t window, int type, bool checkManager = false);

    xcb_atom_t sendTargetsSelection(QMimeData *d, xcb_window_t window, xcb_atom_t property);
    xcb_atom_t sendSelection(QMimeData *d, xcb_atom_t target, xcb_window_t window, xcb_atom_t property);

    xcb_atom_t atomForMode(QClipboard::Mode mode) const;
    QClipboard::Mode modeForAtom(xcb_atom_t atom) const;

    // Selection and Clipboard
    QScopedPointer<MPlatformClipboardMime> m_xClipboard[2];
    QMimeData *m_clientClipboard[2];
    xcb_timestamp_t m_timestamp[2];

    xcb_window_t m_requestor = XCB_NONE;
    xcb_window_t m_owner = XCB_NONE;

    static const int clipboard_timeout;

    int m_maxPropertyRequestDataBytes = 0;
    bool m_clipboard_closing = false;
    xcb_timestamp_t m_incr_receive_time = 0;

    using TransactionMap = QMap<xcb_window_t, XcbClipboardTransaction *>;
    TransactionMap m_transactions;
};

#endif // MPLATFORMCLIPBOARD_H
