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
#include "logging_p.h"

#include <QFile>
#include <QFileSystemWatcher>
#include <QSocketNotifier>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <sys/types.h>
#include <systemd/sd-login.h>

#include <sailfishusermanagerinterface.h>

namespace {

const auto UserDatabaseFile = QStringLiteral("/etc/passwd");
const auto GroupDatabaseFile = QStringLiteral("/etc/group");

enum SpecialIds : uid_t {
    DeviceOwnerId = 100000,
    UnknownCurrentUserId = (uid_t)(-2),
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
    , m_watcher(nullptr)
    , m_alone(Unknown)
{
}

UserInfoPrivate::UserInfoPrivate(struct passwd *pwd)
    : m_uid(pwd->pw_uid)
    , m_username(QString::fromUtf8(pwd->pw_name))
    , m_name(nameFromGecos(pwd->pw_gecos))
    // require_active == true -> only active user is logged in.
    // Specifying seat should make sure that remote users are not
    // counted as they don't have seats.
    , m_loggedIn(sd_uid_is_on_seat(m_uid, 1, "seat0") > 0)
    , m_watcher(nullptr)
    , m_alone(Unknown)
{
}

UserInfoPrivate::~UserInfoPrivate()
{
    delete m_watcher;
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
        // Username is used as displayName only if name is empty, avoid emitting changed twice
        if (m_name.isEmpty() && name.isEmpty())
            emit displayNameChanged();
    }

    if (m_name != name) {
        m_name = name;
        emit nameChanged();
        emit displayNameChanged();
    }
}

bool UserInfoPrivate::alone()
{
    if (m_alone == Unknown)
        updateAlone(true);
    return m_alone == Yes;
}

void UserInfoPrivate::updateAlone(bool force)
{
    if (!force && m_alone == Unknown) {
        // Skip if the value is not needed and the check is not forced
        return;
    }

    Tristated alone = Yes;

    if (m_uid != InvalidId && m_uid != UnknownCurrentUserId && m_uid != DeviceOwnerId) {
        // There must be at least one other user besides device owner
        // if the uid is valid and known and it's not device owner
        alone = No;
    } else {
        // Can not determine from uid, check users group
        errno = 0;
        struct group *grp = getgrnam("users");
        if (!grp) {
            qCWarning(lcUsersLog) << "Could not read users group:" << strerror(errno);
            // Guessing that user is probably alone
        } else {
            for (int i = 0; grp->gr_mem[i] != nullptr; ++i) {
                struct passwd *pwd = getpwnam(grp->gr_mem[i]);
                if (pwd && pwd->pw_uid != DeviceOwnerId) {
                    // Found someone that's not device owner
                    alone = No;
                    break;
                }
                // pwd must not be free'd
            }
            // grp must not be free'd
        }
    }

    if (m_alone != alone) {
        m_alone = alone;
        if (!force) {
            // Emit only if something needed the value already, i.e. it was known
            emit aloneChanged();
        }
    }
}

/**
 * Construct UserInfo for the current user
 *
 * If it has been constructed before, this reuses the old data.
 * If it can not determine the current user, then it constructs
 * an object that doesn't point to any user until a user becomes
 * active on seat0. That should happen very soon after user
 * session has been started.
 */
UserInfo::UserInfo()
{
    d_ptr = UserInfoPrivate::s_current.toStrongRef();
    if (d_ptr.isNull()) {
        uid_t uid = InvalidId;
        struct passwd *pwd;
        if (sd_seat_get_active("seat0", NULL, &uid) >= 0 && uid != InvalidId) {
            if ((pwd = getpwuid(uid))) {
                d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
            } else {
                // User did not exist, should not happen
                d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
            }
        } else {
            // User is not active yet
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
            d_ptr->m_uid = UnknownCurrentUserId;
            waitForActivation();
        }
        // pwd must not be free'd
        if (current())
            UserInfoPrivate::s_current = d_ptr;
    }
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
    auto current_d = UserInfoPrivate::s_current.toStrongRef();
    if (!current_d.isNull() && current_d->m_uid == (uid_t)uid) {
        d_ptr = current_d;
    } else {
        struct passwd *pwd = (uid_t)uid != InvalidId ? getpwuid((uid_t)uid) : nullptr;
        if (pwd) {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
        } else {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
        }
        // pwd must not be free'd
        if (current())
            UserInfoPrivate::s_current = d_ptr;
    }
    connectSignals();
}

/**
 * Construct UserInfo by username
 */
UserInfo::UserInfo(QString username)
{
    auto current_d = UserInfoPrivate::s_current.toStrongRef();
    if (!current_d.isNull() && current_d->m_username == username) {
        d_ptr = current_d;
    } else {
        struct passwd *pwd = getpwnam(username.toUtf8().constData());
        if (pwd) {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate(pwd));
        } else {
            d_ptr = QSharedPointer<UserInfoPrivate>(new UserInfoPrivate);
        }
        // pwd must not be free'd
        if (current())
            UserInfoPrivate::s_current = d_ptr;
    }
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
 * Returns true if the user exists
 */
bool UserInfo::isValid() const
{
    Q_D(const UserInfo);
    return d->m_uid != InvalidId && d->m_uid != UnknownCurrentUserId;
}

QString UserInfo::displayName() const
{
    Q_D(const UserInfo);
    if (d->m_name.isEmpty()) {
        if (type() == DeviceOwner) {
            //: Default value for device owner's name when it is not set
            //% "Device owner"
            return qtTrId("systemsettings-li-device_owner");
        } else if (d->m_uid == SAILFISH_USERMANAGER_GUEST_UID) {
            //: Default value for guest user's name when it is not set
            //% "Guest user"
            return qtTrId("systemsettings-li-guest_user");
        }
        return d->m_username;
    }
    return d->m_name;
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
        if (d->m_name.isEmpty())
            emit d_ptr->displayNameChanged();
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
        emit d_ptr->displayNameChanged();
    }
}

UserInfo::UserType UserInfo::type() const
{
    Q_D(const UserInfo);
    // Device lock considers user with id 100000 as device owner.
    // Some other places consider the user belonging to sailfish-system
    // as device owner. We have to pick one here.
    switch (d->m_uid) {
    case DeviceOwnerId:
        return DeviceOwner;
    case SAILFISH_USERMANAGER_GUEST_UID:
        return Guest;
    default:
        return User;
    }
}

int UserInfo::uid() const
{
    Q_D(const UserInfo);
    return (int)d->m_uid;
}

void UserInfo::setUid(int uid)
{
    Q_D(const UserInfo);
    if ((uid_t)uid != d->m_uid)
        replace(UserInfo(uid).d_ptr);
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

bool UserInfo::updateCurrent()
{
    Q_D(UserInfo);
    bool previous = d->m_loggedIn;
    d->m_loggedIn = sd_uid_is_on_seat(d->m_uid, 1, "seat0") > 0;
    if (d->m_loggedIn != previous) {
        if (d->m_loggedIn)
            UserInfoPrivate::s_current = d_ptr;
        else if (UserInfoPrivate::s_current == d_ptr)
            UserInfoPrivate::s_current.clear();
        emit d_ptr->currentChanged();
        return true;
    }
    return false;
}

/**
 * Returns true if there is only one user on the device
 */
bool UserInfo::alone()
{
    Q_D(UserInfo);
    return d->alone();
}

/**
 * Returns true if object follows database changes, defaults to false
 *
 * Note that even if watched is false, the object can change and emit
 * change signals.
 */
bool UserInfo::watched()
{
    Q_D(const UserInfo);
    return (bool)d->m_watcher;
}

/**
 * If set true object starts to follow database changes.
 * Setting to false is not allowed but it can change back to false
 * if watching fails.
 *
 * Setting to false would be a bit difficult since if some data sharing
 * object would like to stop watching it will end watching for all of
 * them. Thus it's better if you never set this to false. In practice,
 * it's not necessary to set this to false ever.
 */
void UserInfo::setWatched(bool watch)
{
    Q_D(UserInfo);
    // UserInfo objects with uid set to InvalidId can not be watched
    if (d->m_uid != InvalidId && watch && !d->m_watcher) {
        watchForChanges();
        if (d_ptr->m_watcher)
            emit d->watchedChanged();
    }
}

/**
 * Resets object reloading all information
 */
void UserInfo::reset()
{
    Q_D(UserInfo);
    d->set((isValid()) ? getpwuid(d->m_uid) : nullptr);
    updateCurrent();
    d->updateAlone();
}

void UserInfo::replace(QSharedPointer<UserInfoPrivate> other)
{
    auto old = d_ptr;
    disconnect(old.data(), 0, this, 0);
    d_ptr = other;

    if (old->m_username != d_ptr->m_username) {
        emit usernameChanged();
        // Username is used as displayName only if name is empty, avoid emitting changed twice
        if (old->m_name.isEmpty() && d_ptr->m_name.isEmpty())
            emit displayNameChanged();
    }

    if (old->m_name != d_ptr->m_name) {
        emit nameChanged();
        emit displayNameChanged();
    }

    if (old->m_uid != d_ptr->m_uid)
        emit uidChanged();

    if (old->m_loggedIn != d_ptr->m_loggedIn)
        emit currentChanged();

    if (old->m_watcher && !d_ptr->m_watcher) {
        watchForChanges();
        if (!d_ptr->m_watcher)
            emit watchedChanged();
    } else if (!old->m_watcher && d_ptr->m_watcher) {
        emit watchedChanged();
    }

    // If alone value was known, ensure that new d_ptr also knows it
    if (old->m_alone != UserInfoPrivate::Unknown && old->alone() != d_ptr->alone())
        emit aloneChanged();

    connectSignals();
}

UserInfo &UserInfo::operator=(const UserInfo &other)
{
    if (this == &other)
        return *this;

    replace(other.d_ptr);

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
    connect(d_ptr.data(), &UserInfoPrivate::displayNameChanged, this, &UserInfo::displayNameChanged);
    connect(d_ptr.data(), &UserInfoPrivate::usernameChanged, this, &UserInfo::usernameChanged);
    connect(d_ptr.data(), &UserInfoPrivate::nameChanged, this, &UserInfo::nameChanged);
    connect(d_ptr.data(), &UserInfoPrivate::uidChanged, this, &UserInfo::uidChanged);
    connect(d_ptr.data(), &UserInfoPrivate::currentChanged, this, &UserInfo::currentChanged);
    connect(d_ptr.data(), &UserInfoPrivate::watchedChanged, this, &UserInfo::watchedChanged);
    connect(d_ptr.data(), &UserInfoPrivate::aloneChanged, this, &UserInfo::aloneChanged);
}

void UserInfo::watchForChanges()
{
    Q_D(UserInfo);
    d->m_watcher = new QFileSystemWatcher(d);
    QStringList missing = d->m_watcher->addPaths(QStringList() << UserDatabaseFile << GroupDatabaseFile);
    if (missing.count() == 2) {
        qCWarning(lcUsersLog) << "Could not watch for changes in user or group database";
        delete d->m_watcher;
        d->m_watcher = nullptr;
    } else if (missing.count() > 0) {
        qCWarning(lcUsersLog) << "Could not watch for changes in" << missing;
    } else {
        connect(d->m_watcher, &QFileSystemWatcher::fileChanged, d, &UserInfoPrivate::databaseChanged);
    }
}

void UserInfoPrivate::databaseChanged(const QString &path)
{
    if (QFile::exists(path)) {
        if (path == UserDatabaseFile) {
            // User database updated, reset model
            qCDebug(lcUsersLog) << "User database changed, updating data";
            set(getpwuid(m_uid));
        } else if (m_alone != Unknown) { // && path == GroupDatabaseFile
            // Group database updated, update alone status
            qCDebug(lcUsersLog) << "Group database changed, checking alone status again";
            updateAlone();
        }
    }
    if (!m_watcher->files().contains(path)) {
        if (QFile::exists(path) && m_watcher->addPath(path)) {
            qCDebug(lcUsersLog) << "Re-watching" << path << "for changes";
        } else {
            qCWarning(lcUsersLog) << "Stopped watching" << path << "for changes";
        }
    }
}

void UserInfo::waitForActivation()
{
    // Monitor systemd-logind for changes on seats
    sd_login_monitor *monitor;
    if (sd_login_monitor_new("seat", &monitor) < 0) {
        qCWarning(lcUsersLog) << "Could not start monitoring seat changes";
    } else {
        int fd = sd_login_monitor_get_fd(monitor);
        if (fd < 0) {
            qCWarning(lcUsersLog) << "Could not get file descriptor, not monitoring seat changes";
            sd_login_monitor_unref(monitor);
        } else if (!(sd_login_monitor_get_events(monitor) & POLLIN)) {
            // Should not happen
            qCWarning(lcUsersLog) << "Wrong events bits, not monitoring seat changes";
            sd_login_monitor_unref(monitor);
        } else {
            auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(notifier, &QSocketNotifier::activated, this, [this, notifier, monitor](int socket) {
                Q_UNUSED(socket)
                if (uid() != (int)UnknownCurrentUserId) {
                    // This user has been changed to someone else already, stop monitoring
                    qCDebug(lcUsersLog) << "UserInfo uid had been changed";
                    notifier->deleteLater();
                }
                // Check if seat0 has got active user
                uid_t uid = InvalidId;
                if (sd_seat_get_active("seat0", NULL, &uid) >= 0 && uid != InvalidId) {
                    qCDebug(lcUsersLog) << "User activated on seat0";
                    replace(UserInfo().d_ptr);
                    notifier->deleteLater();
                // Otherwise it was not the event we are waiting for, just flush
                } else if (sd_login_monitor_flush(monitor) < 0) {
                    qCWarning(lcUsersLog) << "Monitor flush failed";
                    notifier->deleteLater();
                }
            });
            connect(notifier, &QObject::destroyed, [monitor] {
                qCDebug(lcUsersLog) << "Stopped monitoring seat changes";
                sd_login_monitor_unref(monitor);
            });
            qCDebug(lcUsersLog) << "Started monitoring seat changes";
        }
    }
}
