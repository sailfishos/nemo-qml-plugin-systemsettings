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

#ifndef USERMODEL_H
#define USERMODEL_H

#include <QAbstractListModel>
#include <QDBusError>
#include <QHash>
#include <QSet>
#include <QVector>

#include "systemsettingsglobal.h"
#include "userinfo.h"

class QDBusInterface;
class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
struct SailfishUserManagerEntry;

class SYSTEMSETTINGS_EXPORT UserModel: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool placeholder READ placeholder WRITE setPlaceholder NOTIFY placeholderChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int maximumCount READ maximumCount NOTIFY maximumCountChanged)
    Q_PROPERTY(bool guestEnabled READ guestEnabled WRITE setGuestEnabled NOTIFY guestEnabledChanged)

public:
    enum Roles {
        UsernameRole = Qt::UserRole,
        NameRole,
        TypeRole,
        UidRole,
        CurrentRole,
        PlaceholderRole,
        TransitioningRole,
    };
    Q_ENUM(Roles)

    enum UserType {
        User = 0,
        DeviceOwner = 1,
        Guest = 2,
    };
    Q_ENUM(UserType)

    enum ErrorType {
        Failure = QDBusError::Failed,
        OtherError = QDBusError::Other,
        InvalidArgs = QDBusError::InvalidArgs,
        Busy = 100,
        HomeCreateFailed,
        HomeRemoveFailed,
        GroupCreateFailed,
        UserAddFailed,
        UserModifyFailed,
        UserRemoveFailed,
        GetUidFailed,
        UserNotFound,
        AddToGroupFailed,
        RemoveFromGroupFailed,
        MaximumNumberOfUsersReached,
    };
    Q_ENUM(ErrorType)

    explicit UserModel(QObject *parent = 0);
    ~UserModel();

    bool placeholder() const;
    void setPlaceholder(bool value);
    int count() const;
    int maximumCount() const;

    QHash<int, QByteArray> roleNames() const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;

    // Methods to modify users
    Q_INVOKABLE void createUser();
    Q_INVOKABLE void removeUser(int row);
    Q_INVOKABLE void reset(int row);
    Q_INVOKABLE void setCurrentUser(int row);
    Q_INVOKABLE UserInfo * getCurrentUser() const;

    // Methods to modify user's groups
    Q_INVOKABLE bool hasGroup(int row, const QString &group) const;
    Q_INVOKABLE void addGroups(int row, const QStringList &groups);
    Q_INVOKABLE void removeGroups(int row, const QStringList &groups);

    // Guest methods
    bool guestEnabled() const;
    Q_INVOKABLE void setGuestEnabled(bool enabled);

signals:
    void placeholderChanged();
    void countChanged();
    void maximumCountChanged();
    void guestEnabledChanged();
    void userGroupsChanged(int row);
    void userAddFailed(int error);
    void userModifyFailed(int row, int error);
    void userRemoveFailed(int row, int error);
    void setCurrentUserFailed(int row, int error);
    void addGroupsFailed(int row, int error);
    void removeGroupsFailed(int row, int error);
    void setGuestEnabledFailed(bool enabling, int error);

private slots:
    void onUserAdded(const SailfishUserManagerEntry &entry);
    void onUserModified(uint uid, const QString &newName);
    void onUserRemoved(uint uid);
    void onCurrentUserChanged(uint uid);
    void onCurrentUserChangeFailed(uint uid);
    void onGuestUserEnabled(bool enabled);

    void userAddFinished(QDBusPendingCallWatcher *call);
    void userModifyFinished(QDBusPendingCallWatcher *call, uint uid);
    void userRemoveFinished(QDBusPendingCallWatcher *call, uint uid);
    void setCurrentUserFinished(QDBusPendingCallWatcher *call, uint uid);
    void addToGroupsFinished(QDBusPendingCallWatcher *call, uint uid);
    void removeFromGroupsFinished(QDBusPendingCallWatcher *call, uint uid);
    void enableGuestUserFinished(QDBusPendingCallWatcher *call, bool enabling);

    void createInterface();
    void destroyInterface();

private:
    void add(UserInfo &user);

    QVector<UserInfo> m_users;
    QHash<uint, int> m_uidsToRows;
    QSet<uint> m_transitioning;
    QDBusInterface *m_dBusInterface;
    QDBusServiceWatcher *m_dBusWatcher;
    bool m_guestEnabled;
};
#endif /* USERMODEL_H */
