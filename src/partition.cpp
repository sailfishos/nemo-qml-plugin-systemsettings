/*
 * Copyright (C) 2016 Jolla Ltd. <andrew.den.exter@jolla.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#include "partition_p.h"
#include "partitionmanager_p.h"

static const auto systemdService = QStringLiteral("org.freedesktop.systemd1");
static const auto systemdPath = QStringLiteral("/org/freedesktop/systemd1");
static const auto propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
static const auto unitInterface = QStringLiteral("org.freedesktop.systemd1.Unit");
static const auto managerInterface = QStringLiteral("org.freedesktop.systemd1.Manager");
static const auto activeStateProperty = QStringLiteral("ActiveState");

void PartitionPrivate::getUnit()
{
    if (storageType != Partition::External) {
        return;
    }

    const QString serviceName = QStringLiteral("mount-sd@") + deviceName + QStringLiteral(".service");

    Q_ASSERT(!pendingCall);
    Q_ASSERT(unitPath.isEmpty());

    pendingCall = DBus::call<QDBusObjectPath, QString>(
                this,
                systemdPath,
                managerInterface,
                QStringLiteral("GetUnit"),
                serviceName,
                [this](const QDBusObjectPath &path) {
        pendingCall = nullptr;

        setUnitPath(path.path());
    },          [this](const QDBusError &error) {
        pendingCall = nullptr;

        qWarning("Failed to query the unit path of %s: %s", qPrintable(devicePath), qPrintable(error.message()));
    });
}

void PartitionPrivate::setUnitPath(const QString &path)
{
    unitPath = path;

    QDBusConnection systemBus = QDBusConnection::systemBus();

    if (!systemBus.connect(
                systemdService,
                unitPath,
                propertiesInterface,
                QStringLiteral("PropertiesChanged"),
                this,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)))) {
        qWarning("Failed to connect to unit properties changed signal: %s", qPrintable(systemBus.lastError().message()));
    }

    updateState();
}

void PartitionPrivate::updateState()
{
    if (unitPath.isEmpty()) {
        return;
    }

    delete pendingCall;

    pendingCall = DBus::call<QVariant, QString, QString>(
                this,
                unitPath,
                propertiesInterface,
                QStringLiteral("Get"),
                unitInterface,
                activeStateProperty,
                [this](const QVariant &state) {
        pendingCall = nullptr;

        activeState = state.toString();
        mountFailed = activeState == QStringLiteral("failed");

        if (manager) {
            manager->refresh(this);
        }
    },          [this](const QDBusError &error) {
        pendingCall = nullptr;

        qWarning("Failed to query the active state of %s: %s", qPrintable(devicePath), qPrintable(error.message()));
    });
}

void PartitionPrivate::propertiesChanged(
        const QString &, const QVariantMap &, const QStringList &invalidated)
{
    if (invalidated.contains(activeStateProperty)) {
        updateState();
    }
}

Partition::Partition()
{
}

Partition::Partition(const Partition &partition)
    : d(partition.d)
{
}

Partition::Partition(const QExplicitlySharedDataPointer<PartitionPrivate> &d)
    : d(d)
{
}

Partition &Partition::operator =(const Partition &partition)
{
    d = partition.d;
    return *this;
}

Partition::~Partition()
{
}

bool Partition::operator ==(const Partition &partition) const
{
    return d == partition.d;
}

bool Partition::operator !=(const Partition &partition) const
{
    return d != partition.d;
}

bool Partition::isReadOnly() const
{
    return !d || d->readOnly;
}

Partition::Status Partition::status() const
{
    return d ? d->status : Partition::Unmounted;
}

bool Partition::canMount() const
{
    return d && d->canMount;
}

bool Partition::mountFailed() const
{
    return d && d->mountFailed;
}

Partition::StorageType Partition::storageType() const
{
    return d ? d->storageType : Invalid;
}

QString Partition::devicePath() const
{
    return d ? d->devicePath : QString();
}

QString Partition::mountPath() const
{
    return d ? d->mountPath : QString();
}

QString Partition::filesystemType() const
{
    return d ? d->filesystemType : QString();
}

qint64 Partition::bytesAvailable() const
{
    return d ? d->bytesAvailable : 0;
}

qint64 Partition::bytesTotal() const
{
    return d ? d->bytesTotal : 0;
}

qint64 Partition::bytesFree() const
{
    return d ? d->bytesFree : 0;
}

void Partition::refresh()
{
    if (const auto manager = d ? d->manager : nullptr) {
        manager->refresh(d.data());

        emit manager->partitionChanged(*this);
    }
}
