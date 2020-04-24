/*
 * Copyright (C) 2020 Open Mobile Platform LLC.
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

#ifndef USERINFOPRIVATE_H
#define USERINFOPRIVATE_H

#include <sys/types.h>

#include <QObject>
#include <QString>
#include <QWeakPointer>

class QFileSystemWatcher;

class UserInfoPrivate : public QObject
{
    Q_OBJECT

public:
    UserInfoPrivate();
    UserInfoPrivate(struct passwd *pwd);
    ~UserInfoPrivate();

    enum Tristated {
        Yes = 1,
        No = 0,
        Unknown = -1
    };

    uid_t m_uid;
    QString m_username;
    QString m_name;
    bool m_loggedIn;
    static QWeakPointer<UserInfoPrivate> s_current;
    QFileSystemWatcher *m_watcher;
    Tristated m_alone;

    void set(struct passwd *pwd);
    bool alone();
    void updateAlone(bool force = false);

public slots:
    void databaseChanged(const QString &path);

signals:
    void displayNameChanged();
    void usernameChanged();
    void nameChanged();
    void uidChanged();
    void currentChanged();
    void watchedChanged();
    void aloneChanged();
};

#endif /* USERINFOPRIVATE_H */
