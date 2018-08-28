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

#ifndef UDISKS2_MONITOR_H
#define UDISKS2_MONITOR_H

#include <QObject>
#include <QDBusObjectPath>
#include <QExplicitlySharedDataPointer>
#include <QRegularExpression>
#include <QQueue>
#include <QVariantList>

#include "partitionmodel.h"
#include "partitionmanager_p.h"
#include "udisks2block_p.h"

class PartitionManagerPrivate;

static const QRegularExpression deviceRoot(QStringLiteral("^mmcblk\\d+$"));

namespace UDisks2 {

class Block;
class Job;

struct Operation
{
    Operation(const QString &command, const QString &deviceName, const QString &dbusObjectPath, const QString &type, const QVariantHash &arguments)
        : command(command)
        , deviceName(deviceName)
        , dbusObjectPath(dbusObjectPath)
        , type(type)
        , arguments(arguments)
    {}

    QString command;
    QString deviceName;
    QString dbusObjectPath;
    QString type;
    QVariantHash arguments;
};

class Monitor : public QObject
{
    Q_OBJECT
public:
    explicit Monitor(PartitionManagerPrivate *manager, QObject *parent = nullptr);
    ~Monitor();

    static Monitor *instance();

    void lock(const QString &deviceName);
    void unlock(const QString &deviceName, const QString &passphrase);

    void mount(const QString &deviceName);
    void unmount(const QString &deviceName);

    void format(const QString &deviceName, const QString &type, const QString &label, const QString &passphrase);

signals:
    void status(const QString &deviceName, Partition::Status);
    void errorMessage(const QString &objectPath, const QString &errorName);
    void lockError(Partition::Error error);
    void unlockError(Partition::Error error);
    void mountError(Partition::Error error);
    void unmountError(Partition::Error error);
    void formatError(Partition::Error error);

private slots:
    void interfacesAdded(const QDBusObjectPath &objectPath, const InterfaceAndPropertyMap &interfaces);
    void interfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces);

private:
    void setPartitionProperties(QExplicitlySharedDataPointer<PartitionPrivate> &partition, const UDisks2::Block *blockDevice);
    void updatePartitionProperties(const UDisks2::Block *blockDevice);
    void updatePartitionStatus(const UDisks2::Job *job, bool success);
    bool externalBlockDevice(const QString &objectPathStr) const;

    void startLuksOperation(const QString &deviceName, const QString &dbusMethod, const QString &dbusObjectPath, const QVariantList &arguments);
    void startMountOperation(const QString &deviceName, const QString &dbusMethod, const QString &dbusObjectPath, const QVariantList &arguments);
    void lookupPartitions(PartitionManagerPrivate::Partitions &affectedPartitions, const QStringList &objects);

    void createPartition(const Block *block);
    void createBlockDevice(const QString &path, const InterfaceAndPropertyMap &interfacePropertyMap);

    void doFormat(const QString &deviceName, const QString &dbusObjectPath, const QString &type, const QVariantHash &arguments);
    void getBlockDevices();

    QString objectPath(const QString &deviceName) const;

private:
    static Monitor *sharedInstance;

    QExplicitlySharedDataPointer<PartitionManagerPrivate> m_manager;
    QMap<QString, UDisks2::Job *> m_jobsToWait;
    QMap<QString, UDisks2::Block *> m_blockDevices;

    QQueue<Operation> m_operationQueue;
};

}

#endif
