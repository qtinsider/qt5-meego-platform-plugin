/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qxcbconnection.h"
#include "qxcbkeyboard.h"
#include "qxcbscreen.h"
#include "qxcbwindow.h"
#include "qtouchdevice.h"
#include "QtCore/qmetaobject.h"
#include <qpa/qwindowsysteminterface_p.h>
#include <QDebug>
#include <cmath>

#include <xcb/xinput.h>

using qt_xcb_input_device_event_t = xcb_input_button_press_event_t;

struct qt_xcb_input_event_mask_t {
    xcb_input_event_mask_t header;
    uint32_t               mask;
};

void QXcbConnection::xi2SelectDeviceEvents(xcb_window_t window)
{
    if (window == rootWindow())
        return;

    uint32_t bitMask = XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS;
    bitMask |= XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE;
    bitMask |= XCB_INPUT_XI_EVENT_MASK_MOTION;
    // There is a check for enter/leave events in plain xcb enter/leave event handler,
    // core enter/leave events will be ignored in this case.
    bitMask |= XCB_INPUT_XI_EVENT_MASK_ENTER;
    bitMask |= XCB_INPUT_XI_EVENT_MASK_LEAVE;

    qt_xcb_input_event_mask_t mask;
    mask.header.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
    mask.header.mask_len = 1;
    mask.mask = bitMask;
    xcb_void_cookie_t cookie =
            xcb_input_xi_select_events_checked(xcb_connection(), window, 1, &mask.header);
    xcb_generic_error_t *error = xcb_request_check(xcb_connection(), cookie);
    if (error) {
        qCDebug(lcQpaXInput, "failed to select events, window %x, error code %d", window, error->error_code);
        free(error);
    } else {
        QWindowSystemInterfacePrivate::TabletEvent::setPlatformSynthesizesMouse(false);
    }
}

static inline qreal fixed3232ToReal(xcb_input_fp3232_t val)
{
    return qreal(val.integral) + qreal(val.frac) / (1ULL << 32);
}

void QXcbConnection::xi2SetupDevices()
{
    m_xiMasterPointerIds.clear();

    auto reply = Q_XCB_REPLY(xcb_input_xi_query_device, xcb_connection(), XCB_INPUT_DEVICE_ALL);
    if (!reply) {
        qCDebug(lcQpaXInputDevices) << "failed to query devices";
        return;
    }

    auto it = xcb_input_xi_query_device_infos_iterator(reply.get());
    for (; it.rem; xcb_input_xi_device_info_next(&it)) {
        xcb_input_xi_device_info_t *deviceInfo = it.data;
        if (deviceInfo->type == XCB_INPUT_DEVICE_TYPE_MASTER_POINTER) {
            populateTouchDevices(deviceInfo);
            m_xiMasterPointerIds.append(deviceInfo->deviceid);
            break;
        }
    }

    if (m_xiMasterPointerIds.size() > 1)
        qCDebug(lcQpaXInputDevices) << "multi-pointer X detected";
}

void QXcbConnection::populateTouchDevices(void *info)
{
    m_valuatorInfo.clear();

    auto *deviceInfo = reinterpret_cast<xcb_input_xi_device_info_t *>(info);

    qCDebug(lcQpaXInputDevices) << "input device " << xcb_input_xi_device_info_name(deviceInfo) << "ID" << deviceInfo->deviceid;

    auto classes_it = xcb_input_xi_device_info_classes_iterator(deviceInfo);
    for (; classes_it.rem; xcb_input_device_class_next(&classes_it)) {
        xcb_input_device_class_t *classinfo = classes_it.data;
        switch (classinfo->type) {
        case XCB_INPUT_DEVICE_CLASS_TYPE_VALUATOR: {
            auto *vci = reinterpret_cast<xcb_input_valuator_class_t *>(classinfo);
            const int valuatorAtom = qatom(vci->label);
            qCDebug(lcQpaXInputDevices) << "   has valuator" << atomName(vci->label) << "recognized?" << (valuatorAtom < QXcbAtom::NAtoms);
            ValuatorClassInfo info;
            info.min = fixed3232ToReal(vci->min);
            info.max = fixed3232ToReal(vci->max);
            info.number = vci->number;
            info.label = vci->label;
            m_valuatorInfo.append(info);
            break;
        }
        case XCB_INPUT_DEVICE_CLASS_TYPE_BUTTON: {
            auto *bci = reinterpret_cast<xcb_input_button_class_t *>(classinfo);
            auto reply = Q_XCB_REPLY(xcb_input_xi_get_property, xcb_connection(),
                                     bci->sourceid, 0, atom(QXcbAtom::MaxContacts), XCB_ATOM_ANY, 0, 1);
            if (reply && reply->type == XCB_ATOM_INTEGER && reply->format == 8) {
                quint8 *ptr = reinterpret_cast<quint8 *>(xcb_input_xi_get_property_items(reply.get()));
                maxTouchPoints = ptr[0];
            }
            qCDebug(lcQpaXInputDevices, "   has %d buttons", bci->num_buttons);
            break;
        }
        default:
            break;
        }
    }
    m_touchDevices = new QTouchDevice;
    m_touchDevices->setName(QString::fromUtf8(xcb_input_xi_device_info_name(deviceInfo),
                                              xcb_input_xi_device_info_name_length(deviceInfo)));
    m_touchDevices->setType(QTouchDevice::TouchScreen);
    m_touchDevices->setCapabilities(QTouchDevice::Position | QTouchDevice::NormalizedPosition
                                    | QTouchDevice::Area);
    m_touchDevices->setMaximumTouchPoints(maxTouchPoints);

    qCDebug(lcQpaXInputDevices, "   it's a touchscreen with type %d capabilities 0x%X max touch points %d",
            m_touchDevices->type(), (unsigned int)m_touchDevices->capabilities(),
            m_touchDevices->maximumTouchPoints());

    QWindowSystemInterface::registerTouchDevice(m_touchDevices);
}

static inline qreal fixed1616ToReal(xcb_input_fp1616_t val)
{
    return qreal(val) / 0x10000;
}

void QXcbConnection::xi2HandleEvent(xcb_ge_event_t *event)
{
    auto *xiEvent = reinterpret_cast<qt_xcb_input_device_event_t *>(event);
    int sourceDeviceId = xiEvent->deviceid; // may be the master id
    qt_xcb_input_device_event_t *xiDeviceEvent = nullptr;
    xcb_input_enter_event_t *xiEnterEvent = nullptr;
    QXcbWindowEventListener *eventListener = nullptr;

    switch (xiEvent->event_type) {
    case XCB_INPUT_BUTTON_PRESS:
    case XCB_INPUT_BUTTON_RELEASE:
    case XCB_INPUT_MOTION:
    {
        xiDeviceEvent = xiEvent;
        eventListener = windowEventListenerFromId(xiDeviceEvent->event);
        sourceDeviceId = xiDeviceEvent->sourceid; // use the actual device id instead of the master
        break;
    }
    case XCB_INPUT_ENTER:
    case XCB_INPUT_LEAVE: {
        xiEnterEvent = reinterpret_cast<xcb_input_enter_event_t *>(event);
        eventListener = windowEventListenerFromId(xiEnterEvent->event);
        sourceDeviceId = xiEnterEvent->sourceid; // use the actual device id instead of the master
        break;
    }
    default:
        break;
    }

    if (eventListener) {
        if (eventListener->handleNativeEvent(reinterpret_cast<xcb_generic_event_t *>(event)))
            return;
    }

    if (xiDeviceEvent) {
        switch (xiDeviceEvent->event_type) {
        case XCB_INPUT_BUTTON_PRESS:
        case XCB_INPUT_BUTTON_RELEASE:
        case XCB_INPUT_MOTION:
            if (Q_UNLIKELY(lcQpaXInputEvents().isDebugEnabled()))
                qCDebug(lcQpaXInputEvents, "XI2 touch event type %d seq %d detail %d pos %6.1f, %6.1f root pos %6.1f, %6.1f on window %x",
                        event->event_type, xiDeviceEvent->sequence, xiDeviceEvent->detail,
                        fixed1616ToReal(xiDeviceEvent->event_x), fixed1616ToReal(xiDeviceEvent->event_y),
                        fixed1616ToReal(xiDeviceEvent->root_x), fixed1616ToReal(xiDeviceEvent->root_y),xiDeviceEvent->event);
            if (QXcbWindow *platformWindow = platformWindowFromId(xiDeviceEvent->event))
                xi2ProcessTouch(xiDeviceEvent, platformWindow);
            break;
        }
    } else if (xiEnterEvent && eventListener) {
        switch (xiEnterEvent->event_type) {
        case XCB_INPUT_ENTER:
        case XCB_INPUT_LEAVE:
            eventListener->handleXIEnterLeave(event);
            break;
        }
    }
}

void QXcbConnection::xi2ProcessTouch(void *xiDevEvent, QXcbWindow *platformWindow)
{
    auto *xiDeviceEvent = reinterpret_cast<xcb_input_motion_event_t *>(xiDevEvent);
    QList<QWindowSystemInterface::TouchPoint> touchPoints = m_touchPoints;
    if (touchPoints.count() != maxTouchPoints) {
        // initial event, allocate space for all (potential) touch points
        touchPoints.reserve(maxTouchPoints);
        for (int i = 0; i < maxTouchPoints; ++i) {
            QWindowSystemInterface::TouchPoint tp;
            tp.id = i;
            tp.state = Qt::TouchPointReleased;
            touchPoints << tp;
        }
    }
    qreal x, y, nx, ny;
    qreal w = 0.0, h = 0.0;
    int id;
    unsigned int active = 0;
    for (const ValuatorClassInfo &vci : qAsConst(m_valuatorInfo)) {
        double value;
        if (!xi2GetValuatorValueIfSet(xiDeviceEvent, vci.number, &value))
            continue;
        if (Q_UNLIKELY(lcQpaXInputEvents().isDebugEnabled()))
            qCDebug(lcQpaXInputEvents, "   valuator %20s value %lf from range %lf -> %lf",
                    atomName(vci.label).constData(), value, vci.min, vci.max);
        if (vci.label == atom(QXcbAtom::AbsMTPositionX)) {
            x = value;
            nx = (x - vci.min) / (vci.max - vci.min);
        } else if (vci.label == atom(QXcbAtom::AbsMTPositionY)) {
            y = value;
            ny = (y - vci.min) / (vci.max - vci.min);
        } else if (vci.label == atom(QXcbAtom::AbsMTTouchMajor)) {
            w = value;
        } else if (vci.label == atom(QXcbAtom::AbsMTTouchMinor)) {
            h = value;
        } else if (vci.label == atom(QXcbAtom::AbsMTTrackingID)) {
            id = static_cast<int>(value);
            active |= 1 << id;
            QWindowSystemInterface::TouchPoint &touchPoint = touchPoints[id];
            Qt::TouchPointState newState;
            if (touchPoint.state == Qt::TouchPointReleased) {
                newState = Qt::TouchPointPressed;
            } else {
                if (touchPoint.area.center() != QPointF(x, y)) {
                    newState = Qt::TouchPointMoved;
                } else {
                    newState = Qt::TouchPointStationary;
                }
            }
            touchPoint.state = newState;
            touchPoint.area = QRectF(x - w/2, y - h/2, w, h);
            touchPoint.normalPosition = QPointF(nx, ny);

            if (Q_UNLIKELY(lcQpaXInputEvents().isDebugEnabled()))
                qCDebug(lcQpaXInputEvents) << "   touchpoint "  << touchPoint.id << " state " << touchPoint.state << " pos norm " << touchPoint.normalPosition <<
                    " area " << touchPoint.area;
        }
    }
    // mark previously-active-but-now-inactive touch points as released
    for (int i = 0; i < touchPoints.count(); ++i) {
        if (!(active & (1 << i)) && touchPoints.at(i).state != Qt::TouchPointReleased)
            touchPoints[i].state = Qt::TouchPointReleased;
    }
    QWindowSystemInterface::handleTouchEvent(platformWindow->window(), xiDeviceEvent->time, m_touchDevices, touchPoints);

    if (xiDeviceEvent->event_type == XCB_INPUT_BUTTON_RELEASE) {
        // final event, forget touch state
        m_touchPoints.clear();
    } else {
        // save current state so that we have something to reuse later
        m_touchPoints = touchPoints;
    }
}

bool QXcbConnection::xi2SetMouseGrabEnabled(xcb_window_t w, bool grab)
{
    bool ok = false;

    if (grab) { // grab
        uint32_t mask = XCB_INPUT_XI_EVENT_MASK_BUTTON_PRESS
                | XCB_INPUT_XI_EVENT_MASK_BUTTON_RELEASE
                | XCB_INPUT_XI_EVENT_MASK_MOTION
                | XCB_INPUT_XI_EVENT_MASK_ENTER
                | XCB_INPUT_XI_EVENT_MASK_LEAVE;

        for (int id : qAsConst(m_xiMasterPointerIds)) {
            xcb_generic_error_t *error = nullptr;
            auto cookie = xcb_input_xi_grab_device(xcb_connection(), w, XCB_CURRENT_TIME, XCB_CURSOR_NONE, id,
                                                   XCB_INPUT_GRAB_MODE_22_ASYNC, XCB_INPUT_GRAB_MODE_22_ASYNC,
                                                   false, 1, &mask);
            auto *reply = xcb_input_xi_grab_device_reply(xcb_connection(), cookie, &error);
            if (error) {
                qCDebug(lcQpaXInput, "failed to grab events for device %d on window %x"
                                     "(error code %d)", id, w, error->error_code);
                free(error);
            } else {
                // Managed to grab at least one of master pointers, that should be enough
                // to properly dismiss windows that rely on mouse grabbing.
                ok = true;
            }
            free(reply);
        }
    } else { // ungrab
        for (int id : qAsConst(m_xiMasterPointerIds)) {
            auto cookie = xcb_input_xi_ungrab_device_checked(xcb_connection(), XCB_CURRENT_TIME, id);
            xcb_generic_error_t *error = xcb_request_check(xcb_connection(), cookie);
            if (error) {
                qCDebug(lcQpaXInput, "XIUngrabDevice failed - id: %d (error code %d)", id, error->error_code);
                free(error);
            }
        }
        // XIUngrabDevice does not seem to wait for a reply from X server (similar to
        // xcb_ungrab_pointer). Ungrabbing won't fail, unless NoSuchExtension error
        // has occurred due to a programming error somewhere else in the stack. That
        // would mean that things will crash soon anyway.
        ok = true;
    }

    if (ok)
        m_xiGrab = grab;

    return ok;
}

static int xi2ValuatorOffset(const unsigned char *maskPtr, int maskLen, int number)
{
    int offset = 0;
    for (int i = 0; i < maskLen; i++) {
        if (number < 8) {
            if ((maskPtr[i] & (1 << number)) == 0)
                return -1;
        }
        for (int j = 0; j < 8; j++) {
            if (j == number)
                return offset;
            if (maskPtr[i] & (1 << j))
                offset++;
        }
        number -= 8;
    }
    return -1;
}

bool QXcbConnection::xi2GetValuatorValueIfSet(const void *event, int valuatorNum, double *value)
{
    auto *xideviceevent = static_cast<const qt_xcb_input_device_event_t *>(event);
    auto *buttonsMaskAddr = reinterpret_cast<const unsigned char *>(&xideviceevent[1]);
    auto *valuatorsMaskAddr = buttonsMaskAddr + xideviceevent->buttons_len * 4;
    auto *valuatorsValuesAddr = reinterpret_cast<const xcb_input_fp3232_t *>(valuatorsMaskAddr + xideviceevent->valuators_len * 4);

    int valuatorOffset = xi2ValuatorOffset(valuatorsMaskAddr, xideviceevent->valuators_len, valuatorNum);
    if (valuatorOffset < 0)
        return false;

    *value = valuatorsValuesAddr[valuatorOffset].integral;
    *value += ((double)valuatorsValuesAddr[valuatorOffset].frac / (1 << 16) / (1 << 16));
    return true;
}
