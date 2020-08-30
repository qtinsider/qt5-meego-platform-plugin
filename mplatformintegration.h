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

#ifndef MPLATFORMINTEGRATION_H
#define MPLATFORMINTEGRATION_H

#include <QtGui/private/qtguiglobal_p.h>
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>

#include "mexport.h"

#include <xcb/xcb.h>

class XcbConnection;
class QAbstractEventDispatcher;
class MPlatformNativeInterface;

class M_EXPORT MPlatformIntegration : public QPlatformIntegration
{
public:
    MPlatformIntegration(const QStringList &parameters, int &argc, char **argv);
    ~MPlatformIntegration();

    QPlatformPixmap *createPlatformPixmap(QPlatformPixmap::PixelType type) const override;
    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;

    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const override;

    bool hasCapability(Capability cap) const override;
    QAbstractEventDispatcher *createEventDispatcher() const override;
    void initialize() override;

    QPlatformFontDatabase *fontDatabase() const override;

    QPlatformNativeInterface *nativeInterface()const override;

    QPlatformClipboard *clipboard() const override;

    QPlatformInputContext *inputContext() const override;

    QPlatformServices *services() const override;

    QStringList themeNames() const override;
    QPlatformTheme *createPlatformTheme(const QString &name) const override;
    QVariant styleHint(StyleHint hint) const override;

    bool hasDefaultConnection() const { return !m_connections.isEmpty(); }
    XcbConnection *defaultConnection() const { return m_connections.first(); }

    QByteArray wmClass() const;

    QPlatformSessionManager *createPlatformSessionManager(const QString &id, const QString &key) const override;

    void sync() override;

    void beep() const override;

    static MPlatformIntegration *instance() { return m_instance; }

private:
    QList<XcbConnection *> m_connections;

    QScopedPointer<QPlatformFontDatabase> m_fontDatabase;
    QScopedPointer<MPlatformNativeInterface> m_nativeInterface;

    QScopedPointer<QPlatformInputContext> m_inputContext;

    QScopedPointer<QPlatformServices> m_services;

    mutable QByteArray m_wmClass;
    const char *m_instanceName;
    bool m_canGrab;
    xcb_visualid_t m_defaultVisualId;

    static MPlatformIntegration *m_instance;
};

#endif
