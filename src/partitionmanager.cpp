/*
 * Copyright (c) 2016 - 2020 Jolla Ltd. <andrew.den.exter@jolla.com>
 * Copyright (c) 2019 - 2020 Open Mobile Platform LLC.
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

#include "partitionmanager_p.h"
#include "udisks2monitor_p.h"
#include "udisks2blockdevices_p.h"
#include "logging_p.h"

#include <QFile>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QEvent>
#include <QCoreApplication>

#include <algorithm>
#include <blkid/blkid.h>
#include <limits>
#include <linux/limits.h>
#include <mntent.h>
#include <sys/statvfs.h>
#include <sys/quota.h>
#include <unistd.h>

static const auto userName = QString(qgetenv("USER"));

static const QEvent::Type RefreshFinishedEvent = QEvent::Type(QEvent::User + 1);

class RefreshEvent : public QEvent
{
public:
    RefreshEvent(const PartitionManagerPrivate::PartitionList &partitions)
        : QEvent(RefreshFinishedEvent), m_partitions(partitions)
    {
    }

    PartitionManagerPrivate::PartitionList m_partitions;
};

class StatTask : public QRunnable
{
public:
    StatTask(PartitionManagerPrivate *owner, const PartitionManagerPrivate::PartitionList &partitions)
        : m_owner(owner), m_partitions(partitions)
    {
    }

    void run() override
    {
        PartitionManagerPrivate::PartitionList changedPartitions;

        for (auto partition : m_partitions) {
            qint64 quotaAvailable = std::numeric_limits<qint64>::max();
            struct if_dqblk quota = {};

            if (::quotactl(QCMD(Q_GETQUOTA, USRQUOTA), partition->devicePath.toUtf8().constData(),
                           ::getuid(), (caddr_t)&quota) == 0
                    && quota.dqb_bsoftlimit != 0) {
                quotaAvailable = std::max(static_cast<qint64>(dbtob(quota.dqb_bsoftlimit))
                                          - static_cast<qint64>(quota.dqb_curspace),
                                          0LL);
            }

            struct statvfs64 stat;
            if (::statvfs64(partition->mountPath.toUtf8().constData(), &stat) == 0) {
                qint64 bytesFree = stat.f_bfree * stat.f_frsize;
                qint64 bytesAvailable = std::min((qint64)(stat.f_bavail * stat.f_frsize), quotaAvailable);

                if (partition->bytesFree != bytesFree || partition->bytesAvailable != bytesAvailable) {
                    changedPartitions.append(partition);
                }
                partition->bytesFree = bytesFree;
                partition->bytesAvailable = bytesAvailable;
                partition->bytesTotal = stat.f_blocks * stat.f_frsize;
                partition->readOnly = (stat.f_flag & ST_RDONLY) != 0;
            }
        }

        if (m_owner) {
            QCoreApplication::postEvent(m_owner, new RefreshEvent(changedPartitions));
        }
    }

private:
    QPointer<PartitionManagerPrivate> m_owner;
    PartitionManagerPrivate::PartitionList m_partitions;
};

PartitionManagerPrivate *PartitionManagerPrivate::sharedInstance = nullptr;

PartitionManagerPrivate::PartitionManagerPrivate()
{
    Q_ASSERT(!sharedInstance);

    sharedInstance = this;
    m_udisksMonitor.reset(new UDisks2::Monitor(this));
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::status, this, &PartitionManagerPrivate::status);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::errorMessage, this, &PartitionManagerPrivate::errorMessage);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::lockError, this, &PartitionManagerPrivate::lockError);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::unlockError, this, &PartitionManagerPrivate::unlockError);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::mountError, this, &PartitionManagerPrivate::mountError);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::unmountError, this, &PartitionManagerPrivate::unmountError);
    connect(m_udisksMonitor.data(), &UDisks2::Monitor::formatError, this, &PartitionManagerPrivate::formatError);
    connect(UDisks2::BlockDevices::instance(), &UDisks2::BlockDevices::externalStoragesPopulated,
            this, &PartitionManagerPrivate::externalStoragesPopulatedChanged);

    QVariantMap defaultDrive;
    defaultDrive.insert(QLatin1String("model"), QString());
    defaultDrive.insert(QLatin1String("vendor"), QString());
    defaultDrive.insert(QLatin1String("connectionBus"), Partition::SDIO);

    QExplicitlySharedDataPointer<PartitionPrivate> root(new PartitionPrivate(this));
    root->storageType = Partition::System;
    root->mountPath = QStringLiteral("/");
    root->drive = defaultDrive;

    m_partitions.append(root);

    QExplicitlySharedDataPointer<PartitionPrivate> home(new PartitionPrivate(this));
    home->storageType = Partition::User;
    home->mountPath = QStringLiteral("/home");
    home->drive = defaultDrive;

    m_partitions.append(home);
    refresh(m_partitions);

    // Remove any prospective internal partitions that aren't mounted.
    int internalPartitionCount = 0;
    for (PartitionList::iterator it = m_partitions.begin(); it != m_partitions.end();) {
        auto partition = *it;

        if (partition->storageType & Partition::Internal) {
            if (partition->status == Partition::Mounted) {
                internalPartitionCount += 1;
            } else {
                it = m_partitions.erase(it);
                continue;
            }
        }

        ++it;
    }

    // Check that /home is not actually the same device as /
    if (home->status == Partition::Mounted && root->status == Partition::Mounted &&
        home->devicePath == root->devicePath) {
        m_partitions.erase(m_partitions.begin() + 1);
        --internalPartitionCount;
    }

    if (internalPartitionCount == 1) {
        root->storageType = Partition::Mass;
    }

    if (root->status == Partition::Mounted) {
        m_root = Partition(QExplicitlySharedDataPointer<PartitionPrivate>(root));
    }

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(10);
    connect(&m_refreshTimer, SIGNAL(timeout()),
            this, SLOT(refresh()));
}

PartitionManagerPrivate::~PartitionManagerPrivate()
{
    sharedInstance = nullptr;

    for (auto partition : m_partitions) {
        partition->manager = nullptr;
    }
}

PartitionManagerPrivate *PartitionManagerPrivate::instance()
{
    return sharedInstance ? sharedInstance : new PartitionManagerPrivate;
}

Partition PartitionManagerPrivate::root() const
{
    return m_root;
}

QVector<Partition> PartitionManagerPrivate::partitions(const Partition::StorageTypes types) const
{
    QVector<Partition> partitions;

    for (const auto partition : m_partitions) {
        if (partition->storageType & types) {
            if ((types & Partition::ExcludeParents)
                    && !partitions.isEmpty()
                    && partitions.last().d->isParent(partition)) {
                partitions.last() = Partition(partition);
            } else {
                partitions.append(Partition(partition));
            }
        }
    }

    return partitions;
}

void PartitionManagerPrivate::add(QExplicitlySharedDataPointer<PartitionPrivate> partition)
{
    int insertIndex = 0;
    for (const auto existingPartition : m_partitions) {
        if (existingPartition->drive.value(QLatin1String("connectionBus")).toInt()
                <= partition->drive.value(QLatin1String("connectionBus")).toInt())
            ++insertIndex;
        else
            break;
    }

    m_partitions.insert(insertIndex, partition);
    PartitionList addedPartitions = { partition };
    refresh(addedPartitions);
    emit partitionAdded(Partition(partition));
}

void PartitionManagerPrivate::remove(const PartitionList &partitions)
{
    for (const auto removedPartition : partitions) {
        for (int i = m_partitions.count() - 1; i >= 0 && m_partitions.at(i)->storageType == Partition::External; --i) {
            const auto partition = m_partitions.at(i);
            if (removedPartition->devicePath == partition->devicePath) {
                m_partitions.removeAt(i);
            }
        }

        emit partitionRemoved(Partition(removedPartition));
    }
}

void PartitionManagerPrivate::scheduleRefresh()
{
    m_refreshTimer.start();
}

void PartitionManagerPrivate::refresh()
{
    // assuming changes. maybe should rather detect.
    PartitionList changedPartitions;
    for (int index = 0; index < m_partitions.count(); ++index) {
        const auto partition = m_partitions.at(index);
        if (partition->storageType == Partition::External) {
            changedPartitions.append(partition);
        }
    }

    refresh(m_partitions);

    for (const auto partition : changedPartitions) {
        emit partitionChanged(Partition(partition));
    }
}

void PartitionManagerPrivate::refresh(PartitionPrivate *partition)
{
    refresh(PartitionList() << QExplicitlySharedDataPointer<PartitionPrivate>(partition));

    emit partitionChanged(Partition(QExplicitlySharedDataPointer<PartitionPrivate>(partition)));
}

void PartitionManagerPrivate::refresh(const PartitionList &partitions)
{
    for (auto partition : partitions) {
        if (!partition->valid) {
            if (partition->status != Partition::Formatting) {
                partition->status = partition->activeState == QStringLiteral("activating")
                        ? Partition::Mounting
                        : Partition::Unmounted;
            }
            partition->bytesFree = -1;
            partition->bytesAvailable = -1;
            partition->canMount = false;
            partition->readOnly = true;
            partition->filesystemType.clear();
        }
    }

    FILE *mtab = setmntent("/etc/mtab", "r");
    mntent mountEntry;
    char buffer[3 * PATH_MAX];

    while (getmntent_r(mtab, &mountEntry, buffer, sizeof(buffer))) {
        if (!mountEntry.mnt_fsname || !mountEntry.mnt_dir) {
            continue;
        }

        const QString mountPath = QString::fromUtf8(mountEntry.mnt_dir);
        const QString devicePath = QString::fromUtf8(mountEntry.mnt_fsname);
        const QString deviceName = devicePath.section(QChar('/'), 2);

        for (auto partition : partitions) {
            if (partition->valid
                    || ((partition->status == Partition::Mounted || partition->status == Partition::Mounting)
                        && (partition->storageType != Partition::External))) {
                continue;
            }

            if (((partition->storageType & Partition::Internal)
                            && partition->mountPath == mountPath
                            && devicePath.startsWith(QLatin1Char('/')))
                    || (partition->storageType == Partition::External
                            && partition->devicePath == devicePath)) {
                partition->mountPath = mountPath;
                partition->devicePath = devicePath;
                // There two values wrong for system partitions as devicePath will not start with mmcblk.
                // Currently deviceName and deviceRoot are merely informative data fields.
                partition->deviceName = deviceName;
                partition->deviceRoot = deviceRoot.match(deviceName).hasMatch();
                partition->filesystemType = QString::fromUtf8(mountEntry.mnt_type);
                partition->isSupportedFileSystemType = supportedFileSystems().contains(partition->filesystemType);
                partition->status = partition->activeState == QStringLiteral("deactivating")
                        ? Partition::Unmounting
                        : Partition::Mounted;
                partition->canMount = true;
            }
        }
    }

    endmntent(mtab);

    PartitionList partitionsToStat;

    for (auto partition : partitions) {
        if (partition->status == Partition::Mounted) {
            partitionsToStat.append(partition);
            partitionsToStat.last().detach(); // we don't want to share instances over threads
        }
    }

    if (!partitionsToStat.isEmpty()) {
        QThreadPool::globalInstance()->start(new StatTask(this, partitionsToStat));
    }
}

bool PartitionManagerPrivate::isActionAllowed(const QString &devicePath, const QString &action)
{
    qCInfo(lcMemoryCardLog) << "Is auto:" << UDisks2::BlockDevices::instance()->hintAuto(devicePath);
    if (!UDisks2::BlockDevices::instance()->hintAuto(devicePath)) {
        qCWarning(lcMemoryCardLog) << action << " allowed only allowed for automountable partitions," << devicePath << "is not allowed";
        return false;
    }
    return true;
}

void PartitionManagerPrivate::lock(const QString &devicePath)
{
    if (isActionAllowed(devicePath, QStringLiteral("lock")))
        m_udisksMonitor->lock(devicePath);
}

void PartitionManagerPrivate::unlock(const Partition &partition, const QString &passphrase)
{
    if (isActionAllowed(partition.devicePath(), QStringLiteral("unlock")))
        m_udisksMonitor->unlock(partition.devicePath(), passphrase);
}

void PartitionManagerPrivate::mount(const Partition &partition)
{
    if (isActionAllowed(partition.devicePath(), QStringLiteral("mount")))
        m_udisksMonitor->mount(partition.devicePath());
}

void PartitionManagerPrivate::unmount(const Partition &partition)
{
    if (isActionAllowed(partition.devicePath(), QStringLiteral("unmount")))
        m_udisksMonitor->unmount(partition.devicePath());
}

void PartitionManagerPrivate::format(const QString &devicePath, const QString &filesystemType, const QVariantMap &arguments)
{
    if (isActionAllowed(devicePath, QStringLiteral("format")))
        m_udisksMonitor->format(devicePath, filesystemType, arguments);
}

QString PartitionManagerPrivate::objectPath(const QString &devicePath) const
{
    QString deviceName = devicePath.section(QChar('/'), 2);
    if (UDisks2::BlockDevices::instance()->hintAuto(devicePath)) {
        return UDisks2::BlockDevices::instance()->objectPath(devicePath);
    } else {
        qCWarning(lcMemoryCardLog) << "Object path existing only for external memory cards:" << devicePath;
        return QString();
    }
}

QStringList PartitionManagerPrivate::supportedFileSystems() const
{
    // Query filesystems supported by this device
    // Note this will only find filesystems supported either directly by the
    // kernel, or by modules already loaded.
    QStringList supportedFs;
    QFile filesystems(QStringLiteral("/proc/filesystems"));
    if (filesystems.open(QIODevice::ReadOnly)) {
        QString line = filesystems.readLine();
        while (line.length() > 0) {
            supportedFs << line.trimmed().split('\t').last();
            line = filesystems.readLine();
        }
    }
    return supportedFs;
}

bool PartitionManagerPrivate::externalStoragesPopulated() const
{
    return UDisks2::BlockDevices::instance()->populated();
}

bool PartitionManagerPrivate::event(QEvent *event)
{
    if (event->type() == RefreshFinishedEvent) {
        PartitionList partitions = static_cast<RefreshEvent*>(event)->m_partitions;

        for (auto partition : partitions) {
            for (auto ownPartition : m_partitions) {
                if (ownPartition->mountPath == partition->mountPath) {
                    bool change = false;
                    if (ownPartition->bytesFree != partition->bytesFree) {
                        ownPartition->bytesFree = partition->bytesFree;
                        change = true;
                    }
                    if (ownPartition->bytesAvailable != partition->bytesAvailable) {
                        ownPartition->bytesAvailable = partition->bytesAvailable;
                        change = true;
                    }
                    if (ownPartition->bytesTotal != partition->bytesTotal) {
                        ownPartition->bytesTotal = partition->bytesTotal;
                        change = true;
                    }
                    if (ownPartition->readOnly != partition->readOnly) {
                        ownPartition->readOnly = partition->readOnly;
                        change = true;
                    }

                    if (change)
                        emit partitionChanged(Partition(ownPartition));
                    break;
                }
            }
        }

        return true;
    }

    return QObject::event(event);
}

PartitionManager::PartitionManager(QObject *parent)
    : QObject(parent)
    , d(PartitionManagerPrivate::instance())
{
    connect(d.data(), &PartitionManagerPrivate::partitionChanged, this, &PartitionManager::partitionChanged);
    connect(d.data(), &PartitionManagerPrivate::partitionAdded, this, &PartitionManager::partitionAdded);
    connect(d.data(), &PartitionManagerPrivate::partitionRemoved, this, &PartitionManager::partitionRemoved);
    connect(d.data(), &PartitionManagerPrivate::externalStoragesPopulatedChanged,
            this, &PartitionManager::externalStoragesPopulated);
}

PartitionManager::~PartitionManager()
{
}

Partition PartitionManager::root() const
{
    return d->root();
}

QVector<Partition> PartitionManager::partitions(Partition::StorageTypes types) const
{
    return d->partitions(types);
}

void PartitionManager::refresh()
{
    d->scheduleRefresh();
}
