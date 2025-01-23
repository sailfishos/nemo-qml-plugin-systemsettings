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
#include <QDBusContext>
#include <QExplicitlySharedDataPointer>
#include <QRegularExpression>
#include <QQueue>
#include <QVariantList>

#include "partitionmodel.h"
#include "partitionmanager_p.h"
#include "udisks2defines.h"

class PartitionManagerPrivate;

static const QRegularExpression deviceRoot(QStringLiteral("^mmcblk\\d+$"));

namespace UDisks2 {

class Block;
class BlockDevices;
class Job;

struct Operation
{
    Operation(const QString &command, const QString &devicePath, const QString &dbusObjectPath = QString(),
              const QString &filesystemType = QString(), const QVariantMap &arguments  = QVariantMap())
        : command(command)
        , devicePath(devicePath)
        , dbusObjectPath(dbusObjectPath)
        , filesystemType(filesystemType)
        , arguments(arguments)
    {}

    QString command;
    QString devicePath;
    QString dbusObjectPath;
    QString filesystemType;
    QVariantMap arguments;
};

class Monitor : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    explicit Monitor(PartitionManagerPrivate *manager, QObject *parent = nullptr);
    ~Monitor();

    static Monitor *instance();

    void lock(const QString &devicePath);
    void unlock(const QString &devicePath, const QString &passphrase);

    void mount(const QString &devicePath);
    void unmount(const QString &devicePath);

    void format(const QString &devicePath, const QString &filesystemType, const QVariantMap &arguments);

signals:
    void status(const QString &devicePath, Partition::Status);
    void errorMessage(const QString &objectPath, const QString &errorName);
    void lockError(Partition::Error error);
    void unlockError(Partition::Error error);
    void mountError(Partition::Error error);
    void unmountError(Partition::Error error);
    void formatError(Partition::Error error);

private slots:
    void interfacesAdded(const QDBusObjectPath &objectPath, const UDisks2::InterfacePropertyMap &interfaces);
    void interfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces);
    void doFormat(const QString &devicePath, const QString &dbusObjectPath, const QString &filesystemType,
                  const QVariantMap &arguments);
    void handleNewBlock(UDisks2::Block *block, bool forceCreatePartition);
    void jobCompleted(bool success, const QString &msg);

private:
    void setPartitionProperties(QExplicitlySharedDataPointer<PartitionPrivate> &partition, const Block *blockDevice);
    void updatePartitionProperties(const Block *blockDevice);
    void updatePartitionStatus(const Job *job, bool success);

    void startLuksOperation(const QString &devicePath, const QString &dbusMethod, const QString &dbusObjectPath,
                            const QVariantList &arguments);
    void startMountOperation(const QString &devicePath, const QString &dbusMethod, const QString &dbusObjectPath,
                             const QVariantList &arguments);
    PartitionManagerPrivate::PartitionList lookupPartitions(const QStringList &objects);

    void createPartition(const Block *block);
    void getBlockDevices();
    void connectSignals(UDisks2::Block *block);

private:
    static Monitor *sharedInstance;

    QExplicitlySharedDataPointer<PartitionManagerPrivate> m_manager;
    QMap<QString, Job *> m_jobsToWait;

    QQueue<Operation> m_operationQueue;

    BlockDevices *m_blockDevices;
};

}

#endif
