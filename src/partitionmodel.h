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

#ifndef PARTITIONMODEL_H
#define PARTITIONMODEL_H

#include <QAbstractListModel>

#include <partitionmanager.h>

class SYSTEMSETTINGS_EXPORT PartitionModel : public QAbstractListModel
{
    Q_OBJECT
    Q_ENUMS(Status)
    Q_ENUMS(StorageType)
    Q_FLAGS(StorageTypes)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(StorageTypes storageTypes READ storageTypes WRITE setStorageTypes NOTIFY storageTypesChanged)
public:
    enum {
        ReadOnlyRole,
        StatusRole,
        CanMountRole,
        MountFailedRole,
        StorageTypeRole,
        FilesystemTypeRole,
        DevicePathRole,
        MountPathRole,
        BytesAvailableRole,
        BytesTotalRole,
        BytesFreeRole,
        PartitionModelRole
    };

    enum Status {
        Unmounted       = Partition::Unmounted,
        Mounting        = Partition::Mounting,
        Mounted         = Partition::Mounted,
        Unmounting      = Partition::Unmounting
    };

    enum StorageType {
        Invalid         = Partition::Invalid,
        System          = Partition::System,
        User            = Partition::User,
        Mass            = Partition::Mass,
        External        = Partition::External,

        ExcludeParents = Partition::ExcludeParents,

        Internal        = Partition::Internal,
        Any             = Partition::Any
    };

    Q_DECLARE_FLAGS(StorageTypes, StorageType)

    explicit PartitionModel(QObject *parent = 0);
    ~PartitionModel();

    StorageTypes storageTypes() const;
    void setStorageTypes(StorageTypes storageTypes);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refresh(int index);

    QHash<int, QByteArray> roleNames() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

signals:
    void countChanged();
    void storageTypesChanged();

private:
    void update();

    void partitionChanged(const Partition &partition);
    void partitionAdded(const Partition &partition);
    void partitionRemoved(const Partition &partition);


    QExplicitlySharedDataPointer<PartitionManagerPrivate> m_manager;
    QVector<Partition> m_partitions;
    StorageTypes m_storageTypes;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PartitionModel::StorageTypes)

#endif
