/*
 * Copyright (C) 2020 Chukwudi Nwutobo <nwutobo@outlook.com>
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

#include "mcontextkitproperty.h"

#include <QDBusReply>

static QString objectPathForProperty(const QString& property)
{
    QString path = property;
    if (!path.startsWith(QLatin1Char('/'))) {
        path.replace(QLatin1Char('.'), QLatin1Char('/'));
        path.prepend(QLatin1String("/org/maemo/contextkit/"));
    }
    return path;
}

MContextKitProperty::MContextKitProperty(const QString& serviceName, const QString& propertyName)
    : propertyInterface(serviceName, objectPathForProperty(propertyName),
                        QLatin1String("org.maemo.contextkit.Property"), QDBusConnection::systemBus())
{
    propertyInterface.call("Subscribe");
    connect(&propertyInterface, SIGNAL(ValueChanged(QVariantList, qulonglong)),
            this, SLOT(cacheValue(QVariantList, qulonglong)));

    QDBusMessage reply = propertyInterface.call("Get");
    if (reply.type() == QDBusMessage::ReplyMessage)
        cachedValue = qdbus_cast<QList<QVariant> >(reply.arguments().value(0)).value(0);
}

MContextKitProperty::~MContextKitProperty()
{
    propertyInterface.call("Unsubscribe");
}

QVariant MContextKitProperty::value() const
{
    return cachedValue;
}

void MContextKitProperty::cacheValue(const QVariantList &values, qulonglong)
{
    cachedValue = values.value(0);
    emit valueChanged(cachedValue);
}
