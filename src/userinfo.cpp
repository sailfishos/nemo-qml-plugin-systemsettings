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

#include "userinfo.h"
#include "userinfo_p.h"

#include <pwd.h>
#include <sys/types.h>
#include <systemd/sd-login.h>

namespace {

enum SpecialIds : uid_t {
    DeviceOwnerId = 100000,
    InvalidId = (uid_t)(-1),
};

QString nameFromGecos(const char *gecos)
{
    // typically GECOS has (sub)fields separated by ","
    // and the first one of them is full name of the user.
    // Sometimes it contains just the full name or it might be empty,
    // thus do this on best effort basis.
    auto name = QString::fromUtf8(gecos);
    int i = name.indexOf(QStringLiteral(","));
    if (i != -1)
        name.truncate(i);
    return name;
}

}

UserInfoPrivate::UserInfoPrivate()
    : m_uid(InvalidId)
    , m_loggedIn(false)
{
}

UserInfoPrivate::UserInfoPrivate(struct passwd *pwd)
    : m_uid(pwd->pw_uid)
    , m_username(QString::fromUtf8(pwd->pw_name))
    , m_name(nameFromGecos(pwd->pw_gecos))
    // require_active == false -> both online and active are logged in.
    // Specifying seat should make sure that remote users are not
    // counted as they don't have seats.
    , m_loggedIn(sd_uid_is_on_seat(m_uid, 0, "seat0") > 0)
{
}

UserInfoPrivate::~UserInfoPrivate()
{
}

QWeakPointer<UserInfoPrivate> UserInfoPrivate::s_current;

void UserInfoPrivate::set(struct passwd *pwd)
{
    QString username;
    QString name;

    if (pwd) {
        Q_ASSERT(pwd->pw_uid == m_uid);
        username = QString::fromUtf8(pwd->pw_name);
        name = nameFromGecos(pwd->pw_gecos);
    } else if (m_uid != InvalidId) {
        m_uid = InvalidId;
        emit uidChanged();
    }

    if (m_username != username) {
        m_username = username;
        emit usernameChanged();
    }

    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

/**
 * Construct UserInfo for the current user
 *
 * If it has been constructed before, this reuses the old data.
 */
UserInfo::UserInfo()
{
    if (!UserInfoPrivate::s_current.isNull()) {
        d_ptr = QSharedPointer<UserInfoPrivate>(UserInfoPrivate::s_current);
    }
    if (d_ptr.isNull()) {
        uid_t uid = InvalidId;
        struct passwd *pwd;
        if (sd_seat_get_active("seat0", NULL, &uid) < 0 || uid == InvalidId || !(pwd = getpwuid(uid))) {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
        } else {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
        }
        // pwd must not be free'd
    }
    if (current())
        UserInfoPrivate::s_current = d_ptr;
    connectSignals();
}

UserInfo::UserInfo(const UserInfo &other)
    : QObject(other.parent())
    , d_ptr(other.d_ptr)
{
    connectSignals();
}

/**
 * Construct UserInfo by uid
 */
UserInfo::UserInfo(int uid)
{
    struct passwd *pwd = (uid_t)uid != InvalidId ? getpwuid((uid_t)uid) : nullptr;
    if (pwd) {
        d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
    } else {
        d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
    }
    // pwd must not be free'd
    if (current())
        UserInfoPrivate::s_current = d_ptr;
    connectSignals();
}

/**
 * Construct UserInfo by username
 */
UserInfo::UserInfo(QString username)
{
    struct passwd *pwd = getpwnam(username.toUtf8().constData());
    if (pwd) {
        d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
    } else {
        d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
    }
    // pwd must not be free'd
    if (current())
        UserInfoPrivate::s_current = d_ptr;
    connectSignals();
}

/**
 * Construct a placeholder user that doesn't exist
 *
 * Placeholder users are always invalid.
 */
UserInfo UserInfo::placeholder()
{
    return UserInfo(InvalidId);
}

UserInfo::~UserInfo()
{
}

/**
 * Returns true if user exists
 */
bool UserInfo::isValid() const
{
    Q_D(const UserInfo);
    return d->m_uid != InvalidId;
}

QString UserInfo::username() const
{
    Q_D(const UserInfo);
    return d->m_username;
}

void UserInfo::setUsername(QString username)
{
    Q_D(UserInfo);
    if (d->m_username != username) {
        d->m_username = username;
        emit d_ptr->usernameChanged();
    }
}

QString UserInfo::name() const
{
    Q_D(const UserInfo);
    return d->m_name;
}

void UserInfo::setName(QString name)
{
    Q_D(UserInfo);
    if (d->m_name != name) {
        d->m_name = name;
        emit d_ptr->nameChanged();
    }
}

UserInfo::UserType UserInfo::type() const
{
    Q_D(const UserInfo);
    // Device lock considers user with id 100000 as device owner.
    // Some other places consider the user belonging to sailfish-system
    // as device owner. We have to pick one here.
    return (d->m_uid == DeviceOwnerId) ? DeviceOwner : User;
}

int UserInfo::uid() const
{
    Q_D(const UserInfo);
    return (int)d->m_uid;
}

/**
 * Returs true if user is logged in on seat0 and is the active user, i.e. the current user
 */
bool UserInfo::current() const
{
    Q_D(const UserInfo);
    // Any logged in user (on seat0) must be the current one
    // since we don't have multisession.
    return d->m_loggedIn;
}

void UserInfo::reset()
{
    Q_D(UserInfo);
    d->set((isValid()) ? getpwuid(d->m_uid) : nullptr);
}

UserInfo &UserInfo::operator=(const UserInfo &other)
{
    if (this == &other)
        return *this;

    d_ptr = other.d_ptr;

    return *this;
}

bool UserInfo::operator==(const UserInfo &other) const
{
    if (!isValid())
        return false;
    return d_ptr == other.d_ptr;
}

bool UserInfo::operator!=(const UserInfo &other) const
{
    if (!isValid())
        return true;
    return d_ptr != other.d_ptr;
}

void UserInfo::connectSignals()
{
    connect(d_ptr.data(), &UserInfoPrivate::usernameChanged, this, &UserInfo::usernameChanged);
    connect(d_ptr.data(), &UserInfoPrivate::nameChanged, this, &UserInfo::nameChanged);
    connect(d_ptr.data(), &UserInfoPrivate::uidChanged, this, &UserInfo::uidChanged);
}
