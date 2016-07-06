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

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

class PartitionManagerPrivate;

class PartitionPrivate : public QObject, public QSharedData
{
    Q_OBJECT
public:
    PartitionPrivate(PartitionManagerPrivate *manager)
        : manager(manager)
        , pendingCall(nullptr)
        , bytesAvailable(0)
        , bytesTotal(0)
        , bytesFree(0)
        , storageType(Partition::Invalid)
        , status(Partition::Unmounted)
        , readOnly(true)
        , canMount(false)
        , mountFailed(false)
        , deviceRoot(false)
    {
    }

public slots:
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

public:
    void getUnit();
    void setUnitPath(const QString &path);
    void updateState();
    bool isParent(const QExplicitlySharedDataPointer<PartitionPrivate> &child) const {
        return (deviceRoot && child->deviceName.startsWith(deviceName + QLatin1Char('p')));
    }

    PartitionManagerPrivate *manager;
    QDBusPendingCallWatcher *pendingCall;

    QString unitPath;
    QString deviceName;
    QString devicePath;
    QString mountPath;
    QString filesystemType;
    QString activeState;
    qint64 bytesAvailable;
    qint64 bytesTotal;
    qint64 bytesFree;
    Partition::StorageType storageType;
    Partition::Status status;
    bool readOnly;
    bool canMount;
    bool mountFailed;
    bool deviceRoot;
};

namespace DBus {

inline QVariantList packArguments()
{
    return QVariantList();
}

template <typename Argument> inline QVariantList packArguments(const Argument &argument)
{
    return QVariantList() << QVariant::fromValue(argument);
}

// This should be varidic to support an arbitrary number of arguments, but I seem to be missing
// something obvious and hitting type deduction failures.
template <typename Argument0, typename Argument1> inline QVariantList packArguments(const Argument0 argument0, const Argument1 &argument1)
{
    return QVariantList() << QVariant::fromValue(argument0) << QVariant::fromValue(argument1);
}

template <typename Return, typename... Arguments, typename Finished, typename Error>
QDBusPendingCallWatcher *call(
        QObject *parent,
        const QString &path,
        const QString &interface,
        const QString &method,
        const Arguments&... arguments,
        const Finished &onFinished,
        const Error &onError)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.systemd1"), path, interface, method);
    message.setArguments(packArguments<Arguments ...>(arguments...));

    QDBusConnection systemBus = QDBusConnection::systemBus();

    QDBusPendingCall call = systemBus.asyncCall(message);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, parent);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, [onFinished, onError](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        QDBusPendingReply<Return> reply = *watcher;

        if (reply.isError()) {
            onError(reply.error());
        } else {
            onFinished(reply.value());
        }
    });

    return watcher;
}

template <typename... Arguments>
void invoke(
        const QString &path,
        const QString &interface,
        const QString &method,
        const Arguments&... arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.systemd1"), path, interface, method);
    message.setArguments(packArguments(arguments...));

    QDBusConnection::systemBus().asyncCall(message);
}

}

#endif
