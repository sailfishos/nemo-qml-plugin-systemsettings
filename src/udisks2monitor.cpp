/*
 * Copyright (C) 2018 Jolla Ltd. <raine.makelainen@jolla.com>
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

#include "udisks2monitor_p.h"
#include "udisks2job_p.h"
#include "udisks2defines.h"

#include "partitionmanager_p.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDebug>
#include <QRegularExpression>

UDisks2::Monitor *UDisks2::Monitor::sharedInstance = nullptr;

UDisks2::Monitor *UDisks2::Monitor::instance()
{
    Q_ASSERT(!sharedInstance);

    return sharedInstance;
}

UDisks2::Monitor::Monitor(PartitionManagerPrivate *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    Q_ASSERT(!sharedInstance);
    sharedInstance = this;

    qDBusRegisterMetaType<InterfaceAndPropertyMap>();
    QDBusConnection systemBus = QDBusConnection::systemBus();

    connect(systemBus.interface(), &QDBusConnectionInterface::callWithCallbackFailed, this, [this](const QDBusError &error, const QDBusMessage &call) {
        if (error.name() == UDISKS2_ERROR_DEVICE_BUSY) {
            emit errorMessage(call.path(), error.name());
        }
    });

    if (!systemBus.connect(
                UDISKS2_SERVICE,
                UDISKS2_PATH,
                DBUS_OBJECT_MANAGER_INTERFACE,
                QStringLiteral("InterfacesAdded"),
                this,
                SLOT(interfacesAdded(QDBusObjectPath, InterfaceAndPropertyMap)))) {
        qWarning("Failed to connect to interfaces added signal: %s", qPrintable(systemBus.lastError().message()));
    }

    if (!systemBus.connect(
                UDISKS2_SERVICE,
                UDISKS2_PATH,
                DBUS_OBJECT_MANAGER_INTERFACE,
                QStringLiteral("InterfacesRemoved"),
                this,
                SLOT(interfacesRemoved(QDBusObjectPath, QStringList)))) {
        qWarning("Failed to connect to interfaces added signal: %s", qPrintable(systemBus.lastError().message()));
    }
}

UDisks2::Monitor::~Monitor()
{
    sharedInstance = nullptr;
    qDeleteAll(m_jobsToWait);
    m_jobsToWait.clear();
}

void UDisks2::Monitor::interfacesAdded(const QDBusObjectPath &objectPath, const InterfaceAndPropertyMap &interfaces)
{
    if (interfaces.contains(UDISKS2_PARTITION_INTERFACE) && externalBlockDevice(objectPath.path())) {
        m_manager->refresh();
    } else {
        const QVariantMap dict = interfaces.value(UDISKS2_JOB_INTERFACE);
        const QString operation = dict.value(UDISKS2_JOB_KEY_OPERATION, QString()).toString();
        if (operation == UDISKS2_JOB_OP_FS_MOUNT ||
                operation == UDISKS2_JOB_OP_FS_UNMOUNT ||
                operation == UDISKS2_JOB_OP_CLEANUP) {
            UDisks2::Job *job = new UDisks2::Job(objectPath.path(), dict);
            updateBlockDevState(job, true);

            connect(job, &UDisks2::Job::completed, this, [this](bool success) {
                UDisks2::Job *job = qobject_cast<UDisks2::Job *>(sender());
                updateBlockDevState(job, success);
            });
            m_jobsToWait << job;
        }
    }
}

void UDisks2::Monitor::interfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces)
{
    Q_UNUSED(interfaces)


    // Refresh model when a job that interests us has finished or
    // when block device interface has been removed.
    int jobIndex = -1;
    int index = 0;
    QString path = objectPath.path();
    for (UDisks2::Job *j : m_jobsToWait) {
        if (j->path() == path) {
            j->deleteLater();
            j = nullptr;
            jobIndex = index;
            break;
        }
        ++index;
    }

    if (jobIndex >= 0) {
        m_jobsToWait.removeAt(jobIndex);
    } else if (externalBlockDevice(path)) {
        m_manager->refresh();
    }
}

void UDisks2::Monitor::updateBlockDevState(const UDisks2::Job *job, bool success)
{
    QStringList objects = job->value(UDISKS2_JOB_KEY_OBJECTS).toStringList();
    UDisks2::Job::Operation operation = job->operation();

    if (operation == UDisks2::Job::Mount || operation == UDisks2::Job::Unmount) {
        for (const QString &object : objects) {
            QString deviceName = object.section(QChar('/'), 5);
            for (auto partition : m_manager->m_partitions) {
                if (partition->deviceName == deviceName) {
                    if (success) {
                        if (job->status() == UDisks2::Job::Added) {
                            partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("activating") : QStringLiteral("deactivating");
                            partition->status = operation == UDisks2::Job::Mount ? Partition::Mounting : Partition::Unmounting;
                        } else {
                            // Completed busy unmount job shall stay in mounted state.
                            if (job->deviceBusy() && operation == UDisks2::Job::Unmount)
                                operation = UDisks2::Job::Mount;

                            partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("active") : QStringLiteral("inactive");
                            partition->status = operation == UDisks2::Job::Mount ? Partition::Mounted : Partition::Unmounted;
                        }
                    } else {
                        partition->activeState = QStringLiteral("failed");
                        partition->status = operation == UDisks2::Job::Mount ? Partition::Mounted : Partition::Unmounted;
                    }

                    partition->mountFailed = job->deviceBusy() ? false : !success;

                    m_manager->refresh(partition.data());
                }
            }
        }
    }
}

// Used in UDisks2 InterfacesAdded / InterfacesRemoved signals.
bool UDisks2::Monitor::externalBlockDevice(const QString &objectPathStr) const
{
    static const QRegularExpression externalBlockDevice(QStringLiteral("^/org/freedesktop/UDisks2/block_devices/%1$").arg(externalDevice));
    return externalBlockDevice.match(objectPathStr).hasMatch();
}
