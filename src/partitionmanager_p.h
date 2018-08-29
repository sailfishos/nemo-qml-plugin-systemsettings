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

namespace UDisks2 {
class Monitor;
}

static const auto externalDevice = QStringLiteral("mmcblk(?!0)\\d+(?:p\\d+$)?|(sd[a-z]\\d*)|(dm[_-]\\d+(?:d\\d+)?)");

class PartitionManagerPrivate : public QObject, public QSharedData
{
    Q_OBJECT
public:
    typedef QVector<QExplicitlySharedDataPointer<PartitionPrivate>> Partitions;

    PartitionManagerPrivate();
    ~PartitionManagerPrivate();

    static PartitionManagerPrivate *instance();

    Partition root() const;
    QVector<Partition> partitions(Partition::StorageTypes types) const;

    void add(Partitions &partitions);
    void remove(const Partitions &partitions);

    void refresh();
    void refresh(PartitionPrivate *partition);
    void refresh(const Partitions &partitions, Partitions &changedPartitions);

    void lock(const Partition &partition);
    void unlock(const Partition &partition, const QString &passphrase);
    void mount(const Partition &partition);
    void unmount(const Partition &partition);
    void format(const Partition &partition, const QString &type, const QString &label, const QString &passphrase);

    QStringList supportedFileSystems() const;

signals:
    void partitionChanged(const Partition &partition);
    void partitionAdded(const Partition &partition);
    void partitionRemoved(const Partition &partition);

    void status(const QString &deviceName, Partition::Status);
    void errorMessage(const QString &objectPath, const QString &errorName);
    void lockError(Partition::Error error);
    void unlockError(Partition::Error error);
    void mountError(Partition::Error error);
    void unmountError(Partition::Error error);
    void formatError(Partition::Error error);

private:
    static PartitionManagerPrivate *sharedInstance;

    Partitions m_partitions;
    Partition m_root;

    QScopedPointer<UDisks2::Monitor> m_udisksMonitor;

    // Allow direct access to the Partitions.
    friend class UDisks2::Monitor;
};


#endif

