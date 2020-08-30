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

#include <qpa/qplatformintegrationplugin.h>

#include "mplatformintegration.h"

class MeegoIntegrationPlugin : public QPlatformIntegrationPlugin
{
   Q_OBJECT
   Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "meego.json")
public:
    QPlatformIntegration *create(const QString&, const QStringList&, int &, char **) override;
};

QPlatformIntegration* MeegoIntegrationPlugin::create(const QString& system, const QStringList& parameters, int &argc, char **argv)
{
    if (!system.compare(QLatin1String("meego"), Qt::CaseInsensitive)) {
        auto mIntegration = new MPlatformIntegration(parameters, argc, argv);
        if (!mIntegration->hasDefaultConnection()) {
            delete mIntegration;
            return nullptr;
        }
        return mIntegration;
    }
    return nullptr;
}

#include "main.moc"
