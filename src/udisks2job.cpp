/*
 * Copyright (C) 2018 Jolla Ltd. <raine.makelainen@jolla.com>
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

#include "udisks2job_p.h"
#include "udisks2monitor_p.h"
#include "udisks2defines.h"

#include <QDBusConnection>
#include <QDebug>

#include <nemo-dbus/dbus.h>

UDisks2::Job::Job(const QString &path, const QVariantMap &data, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_data(data)
    , m_status(Added)
    , m_completed(false)
    , m_success(false)
    , m_connection(QDBusConnection::systemBus())
{
    if (!m_connection.connect(
                UDISKS2_SERVICE,
                m_path,
                UDISKS2_JOB_INTERFACE,
                QStringLiteral("Completed"),
                this,
                SLOT(updateCompleted(bool, QString)))) {
        qWarning("Failed to connect to Job's at path %p completed signal: %s: ", qPrintable(m_path), qPrintable(m_connection.lastError().message()));
    }

    // TODO: Move mount / unmount via Udisks2 to PartitionManager

    connect(Monitor::instance(), &Monitor::errorMessage, this, [this](const QString &objectPath, const QString &errorName) {
        QStringList objects = value(UDISKS2_JOB_KEY_OBJECTS).toStringList();
        if (objects.contains(objectPath) && errorName == UDISKS2_ERROR_DEVICE_BUSY) {
            m_message = errorName;
            if (!isCompleted() && deviceBusy()) {
                updateCompleted(false, m_message);
            }
        }
    });
}

UDisks2::Job::~Job()
{
}

bool UDisks2::Job::isCompleted() const
{
    return m_completed;
}

bool UDisks2::Job::success() const
{
    return m_success;
}

QString UDisks2::Job::message() const
{
    return m_message;
}

bool UDisks2::Job::deviceBusy() const
{
    return m_message == UDISKS2_ERROR_TARGET_BUSY || m_message == UDISKS2_ERROR_DEVICE_BUSY;
}

QString UDisks2::Job::path() const
{
    return m_path;
}

QVariant UDisks2::Job::value(const QString &key) const
{
    return NemoDBus::demarshallDBusArgument(m_data.value(key));
}

UDisks2::Job::Status UDisks2::Job::status() const
{
    return m_status;
}

UDisks2::Job::Operation UDisks2::Job::operation() const
{
    QString operation = value(UDISKS2_JOB_KEY_OPERATION).toString();
    if (operation == UDISKS2_JOB_OP_FS_MOUNT) {
        return Mount;
    } else if (operation == UDISKS2_JOB_OP_FS_UNMOUNT) {
        return Unmount;
    } else {
        return Unknown;
    }
}

void UDisks2::Job::updateCompleted(bool success, const QString &message)
{
    m_completed = true;
    m_success = success;
    m_message = message;
    m_status = UDisks2::Job::Completed;

    emit completed(success);

    QDBusConnection systemBus = QDBusConnection::systemBus();
    systemBus.disconnect(
                UDISKS2_SERVICE,
                m_path,
                UDISKS2_JOB_INTERFACE,
                QStringLiteral("Completed"),
                this,
                SLOT(updateJobStatus(bool, QString)));
}
