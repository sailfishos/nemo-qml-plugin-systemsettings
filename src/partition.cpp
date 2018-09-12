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

#include "partition_p.h"
#include "partitionmanager_p.h"

Partition::Partition()
{
}

Partition::Partition(const Partition &partition)
    : d(partition.d)
{
}

Partition::Partition(const QExplicitlySharedDataPointer<PartitionPrivate> &d)
    : d(d)
{
}

Partition &Partition::operator =(const Partition &partition)
{
    d = partition.d;
    return *this;
}

Partition::~Partition()
{
}

bool Partition::operator ==(const Partition &partition) const
{
    return d == partition.d;
}

bool Partition::operator !=(const Partition &partition) const
{
    return d != partition.d;
}

bool Partition::isReadOnly() const
{
    return !d || d->readOnly;
}

Partition::Status Partition::status() const
{
    return d ? d->status : Partition::Unmounted;
}

bool Partition::canMount() const
{
    return d && d->canMount;
}

bool Partition::mountFailed() const
{
    return d && d->mountFailed;
}

bool Partition::isEncrypted() const
{
    return d && d->isEncrypted;
}

bool Partition::isCryptoDevice() const
{
    return d && d->isCryptoDevice;
}

Partition::StorageType Partition::storageType() const
{
    return d ? d->storageType : Invalid;
}

QString Partition::devicePath() const
{
    return d ? d->devicePath : QString();
}

QString Partition::deviceName() const
{
    return d ? d->deviceName : QString();
}

QString Partition::deviceLabel() const
{
    return d ? d->deviceLabel : QString();
}

QString Partition::mountPath() const
{
    return d ? d->mountPath : QString();
}

QString Partition::filesystemType() const
{
    return d ? d->filesystemType : QString();
}

bool Partition::isSupportedFileSystemType() const
{

    return d && d->isSupportedFileSystemType;
}

qint64 Partition::bytesAvailable() const
{
    return d ? d->bytesAvailable : 0;
}

qint64 Partition::bytesTotal() const
{
    return d ? d->bytesTotal : 0;
}

qint64 Partition::bytesFree() const
{
    return d ? d->bytesFree : 0;
}

void Partition::refresh()
{
    if (const auto manager = d ? d->manager : nullptr) {
        manager->refresh(d.data());

        emit manager->partitionChanged(*this);
    }
}
