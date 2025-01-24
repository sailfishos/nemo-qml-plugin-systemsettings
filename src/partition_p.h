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

#ifndef PARTITION_P_H
#define PARTITION_P_H

#include "partition.h"

#include <QVariantMap>

class PartitionManagerPrivate;

class PartitionPrivate : public QSharedData
{
public:
    PartitionPrivate(PartitionManagerPrivate *manager)
        : manager(manager)
        , bytesAvailable(-1)
        , bytesTotal(-1)
        , bytesFree(-1)
        , storageType(Partition::Invalid)
        , status(Partition::Unmounted)
        , readOnly(true)
        , canMount(false)
        , isEncrypted(false)
        , isCryptoDevice(false)
        , isSupportedFileSystemType(false)
        , mountFailed(false)
        , deviceRoot(false)
        , valid(false)
    {
    }

public:
    bool isParent(const QExplicitlySharedDataPointer<PartitionPrivate> &child) const {
        return (deviceRoot && child->deviceName.startsWith(deviceName + QLatin1Char('p')));
    }

    PartitionManagerPrivate *manager;

    QString deviceName;
    QString devicePath;
    QString deviceLabel;
    QString mountPath;
    QString filesystemType;
    QString activeState;
    QString cryptoBackingDevicePath;
    qint64 bytesAvailable;
    qint64 bytesTotal;
    qint64 bytesFree;
    Partition::StorageType storageType;
    Partition::Status status;
    QVariantMap drive;
    bool readOnly;
    bool canMount;
    bool isEncrypted;
    bool isCryptoDevice;
    bool isSupportedFileSystemType;
    bool mountFailed;
    bool deviceRoot;
    // If valid, only mount status and available bytes will be checked
    bool valid;
};

#endif
