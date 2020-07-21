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

#ifndef USERINFO_H
#define USERINFO_H

#include <QObject>
#include <QList>
#include <QSharedPointer>

#include "systemsettingsglobal.h"

class UserInfoPrivate;
class UserModel;

class SYSTEMSETTINGS_EXPORT UserInfo: public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(UserInfo)

    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(UserType type READ type CONSTANT)
    Q_PROPERTY(int uid READ uid WRITE setUid NOTIFY uidChanged)
    Q_PROPERTY(bool current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool alone READ alone NOTIFY aloneChanged)
    Q_PROPERTY(bool watched READ watched WRITE setWatched NOTIFY watchedChanged)

    friend class UserModel;

public:
    enum UserType {
        User = 0,
        DeviceOwner = 1,
        Guest = 2,
    };
    Q_ENUM(UserType)

    UserInfo();
    UserInfo(const UserInfo &other);
    ~UserInfo();

    static UserInfo placeholder();

    bool isValid() const;

    QString displayName() const;
    QString username() const;
    QString name() const;
    UserType type() const;
    int uid() const;
    void setUid(int uid);
    bool current() const;
    bool alone();
    bool watched();
    void setWatched(bool watch);

    Q_INVOKABLE void reset();

    UserInfo &operator=(const UserInfo &other);
    bool operator==(const UserInfo &other) const;
    bool operator!=(const UserInfo &other) const;

signals:
    void displayNameChanged();
    void usernameChanged();
    void nameChanged();
    void uidChanged();
    void currentChanged();
    void aloneChanged();
    void watchedChanged();

private:
    explicit UserInfo(int uid);
    explicit UserInfo(QString username);

    void setUsername(QString username);
    void setName(QString name);
    bool updateCurrent();
    void replace(QSharedPointer<UserInfoPrivate> other);

    void connectSignals();
    void watchForChanges();
    void waitForActivation();

    QSharedPointer<UserInfoPrivate> d_ptr;
};
#endif /* USERINFO_H */
