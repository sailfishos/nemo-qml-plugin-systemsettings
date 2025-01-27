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

#ifndef PARTITIONMANAGER_P_H
#define PARTITIONMANAGER_P_H

#include "partitionmanager.h"
#include "partition_p.h"

#include <QMap>
#include <QVector>
#include <QScopedPointer>
#include <QTimer>

namespace UDisks2 {
class Monitor;
}


class PartitionManagerPrivate : public QObject, public QSharedData
{
    Q_OBJECT
public:
    typedef QVector<QExplicitlySharedDataPointer<PartitionPrivate>> PartitionList;

    PartitionManagerPrivate();
    ~PartitionManagerPrivate();

    static PartitionManagerPrivate *instance();

    Partition root() const;
    QVector<Partition> partitions(Partition::StorageTypes types) const;

    void add(QExplicitlySharedDataPointer<PartitionPrivate> partition);
    void remove(const PartitionList &partitions);

    void scheduleRefresh();
    void refresh(PartitionPrivate *partition);
    void refresh(const PartitionList &partitions, PartitionList &changedPartitions);

    void lock(const QString &devicePath);
    void unlock(const Partition &partition, const QString &passphrase);
    void mount(const Partition &partition);
    void unmount(const Partition &partition);
    void format(const QString &devicePath, const QString &filesystemType, const QVariantMap &arguments);

    QString objectPath(const QString &devicePath) const;

    QStringList supportedFileSystems() const;
    bool externalStoragesPopulated() const;

public slots:
    void refresh();

signals:
    void partitionChanged(const Partition &partition);
    void partitionAdded(const Partition &partition);
    void partitionRemoved(const Partition &partition);
    void externalStoragesPopulatedChanged();

    void status(const QString &deviceName, Partition::Status);
    void errorMessage(const QString &objectPath, const QString &errorName);
    void lockError(Partition::Error error);
    void unlockError(Partition::Error error);
    void mountError(Partition::Error error);
    void unmountError(Partition::Error error);
    void formatError(Partition::Error error);

private:
    bool isActionAllowed(const QString &devicePath, const QString &action);
    // TODO: This is leaking (Disks2::Monitor is never free'ed).
    static PartitionManagerPrivate *sharedInstance;

    PartitionList m_partitions;
    Partition m_root;
    QTimer m_refreshTimer;

    QScopedPointer<UDisks2::Monitor> m_udisksMonitor;

    // Allow direct access to the Partitions.
    friend class UDisks2::Monitor;
};

#endif

