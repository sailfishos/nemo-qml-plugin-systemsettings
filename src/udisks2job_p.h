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

#ifndef UDISKS2_JOB_H
#define UDISKS2_JOB_H

#include <QObject>
#include <QDBusConnection>
#include <QString>
#include <QVariantMap>

namespace UDisks2 {

class Job : public QObject
{
    Q_OBJECT
public:
    Job(const QString &path, const QVariantMap &data, QObject *parent = nullptr);
    ~Job();

    enum Status {
        Added,
        Completed
    };
    Q_ENUM(Status)

    enum Operation {
        Lock,
        Unlock,
        Mount,
        Unmount,
        Format,
        Unknown
    };
    Q_ENUM(Operation)

    void complete(bool success);
    bool isCompleted() const;
    bool success() const;
    QString message() const;
    bool deviceBusy() const;

    QStringList objects() const;

    QString path() const;
    QVariant value(const QString &key) const;

    Status status() const;
    Operation operation() const;

    void dumpInfo() const;

signals:
    void completed(bool success);

private slots:
    void updateCompleted(bool success, const QString &message);

private:
    QString m_path;
    QVariantMap m_data;
    Status m_status;

    QString m_message;
    bool m_completed;
    bool m_success;
};
}

#endif
