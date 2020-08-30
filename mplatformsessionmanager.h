/*
 * Copyright (C) 2020 Chukwudi Nwutobo <nwutobo@outlook.com>
 * Copyright (C) 2018 The Qt Company Ltd.
 * Copyright (C) 2013 Teo Mrnjavac <teo@kde.org>
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

#ifndef MPLATFORMSESSIONMANAGER_H
#define MPLATFORMSESSIONMANAGER_H

#include <qpa/qplatformsessionmanager.h>

class QEventLoop;

class MPlatformSessionManager : public QPlatformSessionManager
{
public:
    MPlatformSessionManager(const QString &id, const QString &key);
    virtual ~MPlatformSessionManager();

    void *handle() const;

    void setSessionId(const QString &id) { m_sessionId = id; }
    void setSessionKey(const QString &key) { m_sessionKey = key; }

    bool allowsInteraction() override;
    bool allowsErrorInteraction() override;
    void release() override;

    void cancel() override;

    void setManagerProperty(const QString &name, const QString &value) override;
    void setManagerProperty(const QString &name, const QStringList &value) override;

    bool isPhase2() const override;
    void requestPhase2() override;

    void exitEventLoop();

private:
    QEventLoop *m_eventLoop;
};

#endif // MPLATFORMSESSIONMANAGER_H
