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

#include "partitionmodel.h"
#include "partitionmanager_p.h"

#include "logging_p.h"

#include <QDir>
#include <QFileInfo>
#include <QtQml/qqmlinfo.h>

PartitionModel::PartitionModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(PartitionManagerPrivate::instance())
    , m_storageTypes(Any | ExcludeParents)
{
    m_partitions = m_manager->partitions(Partition::Any | Partition::ExcludeParents);

    connect(m_manager.data(), &PartitionManagerPrivate::partitionChanged, this, &PartitionModel::partitionChanged);
    connect(m_manager.data(), &PartitionManagerPrivate::partitionAdded, this, &PartitionModel::partitionAdded);
    connect(m_manager.data(), &PartitionManagerPrivate::partitionRemoved, this, &PartitionModel::partitionRemoved);
    connect(m_manager.data(), &PartitionManagerPrivate::externalStoragesPopulatedChanged,
            this, &PartitionModel::externalStoragesPopulatedChanged);

    connect(m_manager.data(), &PartitionManagerPrivate::errorMessage, this, &PartitionModel::errorMessage);

    connect(m_manager.data(), &PartitionManagerPrivate::lockError, this, [this](Partition::Error error) {
        emit lockError(static_cast<PartitionModel::Error>(error));
    });
    connect(m_manager.data(), &PartitionManagerPrivate::unlockError, this, [this](Partition::Error error) {
        emit unlockError(static_cast<PartitionModel::Error>(error));
    });

    connect(m_manager.data(), &PartitionManagerPrivate::mountError, this, [this](Partition::Error error) {
        emit mountError(static_cast<PartitionModel::Error>(error));
    });
    connect(m_manager.data(), &PartitionManagerPrivate::unmountError, this, [this](Partition::Error error) {
        emit unmountError(static_cast<PartitionModel::Error>(error));
    });
    connect(m_manager.data(), &PartitionManagerPrivate::formatError, this, [this](Partition::Error error) {
        emit formatError(static_cast<PartitionModel::Error>(error));
    });
}

PartitionModel::~PartitionModel()
{
}

PartitionModel::StorageTypes PartitionModel::storageTypes() const
{
    return m_storageTypes;
}

void PartitionModel::setStorageTypes(StorageTypes types)
{
    if (m_storageTypes != types) {
        m_storageTypes = types;

        update();

        emit storageTypesChanged();
    }
}

QStringList PartitionModel::supportedFormatTypes() const
{
    QStringList types;
    QDir dir("/sbin/");
    QStringList entries = dir.entryList(QStringList() << QString("mkfs.*"));
    for (const QString &entry : entries) {
        QFileInfo info(QString("/sbin/%1").arg(entry));
        if (info.exists() && info.isExecutable()) {
            QStringList parts = entry.split('.');
            if (!parts.isEmpty()) {
                types << parts.takeLast();
            }
        }
    }

    return types;
}

bool PartitionModel::externalStoragesPopulated() const
{
    return m_manager->externalStoragesPopulated();
}

void PartitionModel::refresh()
{
    m_manager->refresh();
}

void PartitionModel::refresh(int index)
{
    if (index >= 0 && index < m_partitions.count()) {
        m_partitions[index].refresh();
    }
}

void PartitionModel::lock(const QString &devicePath)
{
    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath << m_partitions.count();
    m_manager->lock(devicePath);
}

void PartitionModel::unlock(const QString &devicePath, const QString &passphrase)
{
    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath << m_partitions.count();
    if (const Partition *partition = getPartition(devicePath)) {
        m_manager->unlock(*partition, passphrase);
    } else {
        qCWarning(lcMemoryCardLog) << "Unable to unlock unknown device:" << devicePath;
    }
}

void PartitionModel::mount(const QString &devicePath)
{
    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath << m_partitions.count();
    if (const Partition *partition = getPartition(devicePath)) {
        m_manager->mount(*partition);
    } else {
        qCWarning(lcMemoryCardLog) << "Unable to mount unknown device:" << devicePath;
    }
}

void PartitionModel::unmount(const QString &devicePath)
{
    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath << m_partitions.count();
    if (const Partition *partition = getPartition(devicePath)) {
        m_manager->unmount(*partition);
    } else {
        qCWarning(lcMemoryCardLog) << "Unable to unmount unknown device:" << devicePath;
    }
}

void PartitionModel::format(const QString &devicePath, const QVariantMap &arguments)
{
    QString filesystemType = arguments.value(QLatin1String("filesystemType"), QString()).toString();
    if (filesystemType.isEmpty()) {
        qmlInfo(this) << "Missing or empty filesystemType argument, cannot format.";
        return;
    }

    // Only fixing invalid args would be enough. Udisks don't care if key is unknown like auto-mount.
    QVariantMap args;
    args.insert(QLatin1String("label"), arguments.value(QLatin1String("label"), QString()).toString());
    args.insert(QLatin1String("no-block"), true);
    args.insert(QLatin1String("take-ownership"), true);
    // set-group-permissions is a custom option patched into udisks2 (JB#50288)
    args.insert(QLatin1String("set-group-permissions"), true);
    args.insert(QLatin1String("update-partition-type"), true);
    args.insert(QLatin1String("auto-mount"), arguments.value(QLatin1String("auto-mount"), false).toBool());

    QString passphrase = arguments.value(QLatin1String("encrypt-passphrase"), QString()).toString();
    if (!passphrase.isEmpty()) {
        args.insert(QLatin1String("encrypt.passphrase"), passphrase);
    }

    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath << filesystemType << args << m_partitions.count();
    m_manager->format(devicePath, filesystemType, args);
}

QString PartitionModel::objectPath(const QString &devicePath) const
{
    qCInfo(lcMemoryCardLog) << Q_FUNC_INFO << devicePath;
    return m_manager->objectPath(devicePath);
}

void PartitionModel::update()
{
    const int count = m_partitions.count();

    const auto partitions = m_manager->partitions(Partition::StorageTypes(int(m_storageTypes)));

    int index = 0;

    for (const auto partition : partitions) {
        const int existingIndex = [this, index, partition]() {
            for (int i = index; i < m_partitions.count(); ++i) {
                if (m_partitions.at(i) == partition) {
                    return i;
                }
            }
            return -1;
        }();

        if (existingIndex == -1) {
            beginInsertRows(QModelIndex(), index, index);
            m_partitions.insert(index, partition);
            endInsertRows();
        } else if (existingIndex > index) {
            beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), index);
            const auto partition = m_partitions.takeAt(existingIndex);
            m_partitions.insert(index, partition);
            endMoveRows();
        }
        ++index;
    }

    if (index < m_partitions.count()) {
        beginRemoveRows(QModelIndex(), index, m_partitions.count() - 1);
        m_partitions.resize(index);
        endRemoveRows();
    }

    if (count != m_partitions.count()) {
        emit countChanged();
    }
}

const Partition *PartitionModel::getPartition(const QString &devicePath) const
{
    for (const Partition &partition : m_partitions) {
        if (devicePath == partition.devicePath()) {
            return &partition;
        }
    }

    return nullptr;
}

QHash<int, QByteArray> PartitionModel::roleNames() const
{
    static const QHash<int, QByteArray> roleNames = {
        { ReadOnlyRole, "readOnly" },
        { StatusRole, "status" },
        { CanMountRole, "canMount" },
        { MountFailedRole, "mountFailed" },
        { StorageTypeRole, "storageType" },
        { FilesystemTypeRole, "filesystemType" },
        { DeviceLabelRole, "deviceLabel" },
        { DevicePathRole, "devicePath" },
        { DeviceNameRole, "deviceName" },
        { MountPathRole, "mountPath" },
        { BytesAvailableRole, "bytesAvailable" },
        { BytesTotalRole, "bytesTotal" },
        { BytesFreeRole, "bytesFree" },
        { PartitionModelRole, "partitionModel" },
        { IsCryptoDeviceRoles, "isCryptoDevice"},
        { IsSupportedFileSystemType, "isSupportedFileSystemType"},
        { IsEncryptedRoles, "isEncrypted"},
        { CryptoBackingDevicePath, "cryptoBackingDevicePath"},
        { DriveRole, "drive"},
    };

    return roleNames;
}

int PartitionModel::rowCount(const QModelIndex &parent) const
{
    return !parent.isValid() ? m_partitions.count() : 0;
}

QVariant PartitionModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_partitions.count() || index.column() != 0) {
        return QVariant();
    } else {
        const Partition &partition = m_partitions.at(index.row());

        switch (role) {
        case ReadOnlyRole:
            return partition.isReadOnly();
        case StatusRole:
            return partition.status();
        case CanMountRole:
            return partition.canMount();
        case MountFailedRole:
            return partition.mountFailed();
        case StorageTypeRole:
            return partition.storageType();
        case FilesystemTypeRole:
            return partition.filesystemType();
        case DeviceLabelRole:
            return partition.deviceLabel();
        case DevicePathRole:
            return partition.devicePath();
        case DeviceNameRole:
            return partition.deviceName();
        case MountPathRole:
            return partition.mountPath();
        case BytesAvailableRole:
            return partition.bytesAvailable();
        case BytesTotalRole:
            return partition.bytesTotal();
        case BytesFreeRole:
            return partition.bytesFree();
        case PartitionModelRole:
            return QVariant::fromValue(static_cast<QObject*>(const_cast<PartitionModel*>((this))));
        case IsCryptoDeviceRoles:
            return partition.isCryptoDevice();
        case IsSupportedFileSystemType:
            return partition.isSupportedFileSystemType();
        case IsEncryptedRoles:
            return partition.isEncrypted();
        case CryptoBackingDevicePath:
            return partition.cryptoBackingDevicePath();
        case DriveRole:
            return partition.drive();
        default:
            return QVariant();
        }
    }
}

void PartitionModel::partitionChanged(const Partition &partition)
{
    for (int i = 0; i < m_partitions.count(); ++i) {
        qCInfo(lcMemoryCardLog) << "partition changed:" << partition.status() << partition.mountPath();;
        if (m_partitions.at(i) == partition) {
            QModelIndex index = createIndex(i, 0);
            emit dataChanged(index, index);
            return;
        }
    }
}

void PartitionModel::partitionAdded(const Partition &partition)
{
    if (partition.storageType() & m_storageTypes) {
        update();
    }
}

void PartitionModel::partitionRemoved(const Partition &partition)
{
    for (int i = 0; i < m_partitions.count(); ++i) {
        if (m_partitions.at(i) == partition) {
            beginRemoveRows(QModelIndex(), i, i);
            m_partitions.removeAt(i);
            endRemoveRows();

            emit countChanged();

            return;
        }
    }
}
