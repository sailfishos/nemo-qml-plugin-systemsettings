/*
 * Copyright (c) 2018 - 2019 Jolla Ltd.
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef UDISKS2_BLOCK_H
#define UDISKS2_BLOCK_H

#include <nemo-dbus/connection.h>

#include <QObject>
#include <QVariantMap>
#include <QPointer>
#include <functional>

#include <systemsettingsglobal.h>

#include "udisks2defines.h"

class QDBusPendingCallWatcher;

namespace UDisks2 {


class SYSTEMSETTINGS_EXPORT Block : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionBus READ connectionBus NOTIFY updated)

public:
    Block(const QString &path, const UDisks2::InterfacePropertyMap &interfacePropertyMap, QObject *parent = nullptr);
    virtual ~Block();

    QString path() const;

    QString device() const;
    QString preferredDevice() const;
    QString drive() const;
    QString driveModel() const;
    QString driveVendor() const;
    QString connectionBus() const;

    QString partitionTable() const;
    bool isPartition() const;
    bool isPartitionTable() const;

    qint64 deviceNumber() const;
    QString id() const;

    qint64 size() const;

    bool isCryptoBlock() const;

    bool hasCryptoBackingDevice() const;
    QString cryptoBackingDevicePath() const;
    QString cryptoBackingDeviceObjectPath() const;

    bool isEncrypted() const;
    bool isMountable() const;

    bool isFormatting() const;
    bool setFormatting(bool formatting);

    bool isLocking() const;
    void setLocking();

    bool isReadOnly() const;
    bool hintAuto() const;

    bool isValid() const;

    QString idType() const;
    QString idVersion() const;
    QString idLabel() const;
    QString idUUID() const;

    QStringList symlinks() const;

    QString mountPath() const;

    QVariant value(const QString &key) const;

    bool hasData() const;

    void dumpInfo() const;

    static QString cryptoBackingDevicePath(const QString &objectPath);

signals:
    void completed(QPrivateSignal);
    void updated();
    void formatted();
    void mountPathChanged();
    void blockRemoved(const QString &devicePath);

private slots:
    void updateProperties(const QDBusMessage &message);
    void complete();

private:
    Block& operator=(const Block& other);

    bool setEncrypted(bool encrypted);
    bool setMountable(bool mountable);

    void addInterface(const QString &interface, QVariantMap propertyMap);
    void removeInterface(const QString &interface);
    int interfaceCount() const;
    bool hasInterface(const QString &interface) const;

    bool isCompleted() const;

    void updateFileSystemInterface(const QVariant &mountPoints);
    bool clearFormattingState();

    void getProperties(const QString &path, const QString &interface,
                       bool *pending,
                       std::function<void (const QVariantMap &)> success,
                       std::function<void ()> failed = [](){});

    void rescan(const QString &dbusObjectPath);

    QString m_path;
    UDisks2::InterfacePropertyMap m_interfacePropertyMap;
    QVariantMap m_data;
    QVariantMap m_drive;
    NemoDBus::Connection m_connection;
    QString m_mountPath;
    bool m_mountable;
    bool m_encrypted;
    bool m_formatting;
    bool m_locking;

    bool m_overrideHintAuto = false;

    bool m_pendingFileSystem = false;
    bool m_pendingBlock = false;
    bool m_pendingEncrypted = false;
    bool m_pendingDrive = false;
    bool m_pendingPartition = false;
    bool m_pendingPartitionTable = false;

    friend class BlockDevices;
};

}

#endif
