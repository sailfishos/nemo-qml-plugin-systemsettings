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

#ifndef PARTITION_H
#define PARTITION_H

#include <QSharedData>
#include <QObject>

#include <systemsettingsglobal.h>

class PartitionPrivate;

class SYSTEMSETTINGS_EXPORT Partition
{
public:
    enum StorageType
    {
        Invalid     = 0x00,
        System      = 0x01,
        User        = 0x02,
        Mass        = 0x04,
        External    = 0x08,

        ExcludeParents = 0x1000,

        Internal = System | User | Mass,
        Any = System | User | Mass | External
    };

    enum ConnectionBus {
        SDIO,
        USB,
        IEEE1394,
        UnknownBus
    };

    enum Status {
        Unmounted,
        Mounting,
        Mounted,
        Unmounting,
        Formatting,
        Formatted,
        Unlocking,
        Unlocked,
        Locking,
        Locked
    };

    Q_DECLARE_FLAGS(StorageTypes, StorageType)

    enum Error {
        ErrorFailed,
        ErrorCancelled,
        ErrorAlreadyCancelled,
        ErrorNotAuthorized,
        ErrorNotAuthorizedCanObtain,
        ErrorNotAuthorizedDismissed,
        ErrorAlreadyMounted,
        ErrorNotMounted,
        ErrorOptionNotPermitted,
        ErrorMountedByOtherUser,
        ErrorAlreadyUnmounting,
        ErrorNotSupported,
        ErrorTimedout,
        ErrorWouldWakeup,
        ErrorDeviceBusy
    };

    Partition();
    Partition(const Partition &partition);
    Partition &operator =(const Partition &partition);
    ~Partition();

    bool operator ==(const Partition &partition) const;
    bool operator !=(const Partition &partition) const;

    bool isReadOnly() const;

    Status status() const;

    bool canMount() const;
    bool mountFailed() const;
    bool isCryptoDevice() const;
    bool isEncrypted() const;
    QString cryptoBackingDevicePath() const;

    StorageType storageType() const;
    QVariantMap drive() const;

    QString devicePath() const;
    QString deviceName() const;
    QString deviceLabel() const;
    QString mountPath() const;

    QString filesystemType() const;
    bool isSupportedFileSystemType() const;

    qint64 bytesAvailable() const;
    qint64 bytesTotal() const;
    qint64 bytesFree() const;

    void refresh();

private:
    friend class PartitionManagerPrivate;

    explicit Partition(const QExplicitlySharedDataPointer<PartitionPrivate> &d);

    QExplicitlySharedDataPointer<PartitionPrivate> d;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Partition::StorageTypes)

#endif
