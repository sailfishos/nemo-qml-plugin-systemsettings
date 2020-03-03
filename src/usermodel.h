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
#include <QHash>
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

public:
    enum Roles {
        UsernameRole = Qt::UserRole,
        NameRole,
        TypeRole,
        UidRole,
        CurrentRole,
        PlaceholderRole,
    };
    Q_ENUM(Roles)

    enum UserType {
        User = 0,
        DeviceOwner = 1,
    };
    Q_ENUM(UserType)

    explicit UserModel(QObject *parent = 0);
    ~UserModel();

    bool placeholder();
    void setPlaceholder(bool value);

    QHash<int, QByteArray> roleNames() const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole);
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;

    Q_INVOKABLE void createUser();
    Q_INVOKABLE void removeUser(int row);
    Q_INVOKABLE void reset(int row);
    Q_INVOKABLE UserInfo * getCurrentUser() const;

signals:
    void placeholderChanged();
    void userAddFailed();
    void userModifyFailed(int row, const QString &name);
    void userRemoveFailed(int row, const QString &name);

private slots:
    void userAdded(const SailfishUserManagerEntry &entry);
    void userModified(uint uid, const QString &newName);
    void userRemoved(uint uid);
    void userAddFinished(QDBusPendingCallWatcher *call, int row);
    void userModifyFinished(QDBusPendingCallWatcher *call, int row);
    void userRemoveFinished(QDBusPendingCallWatcher *call, int row);
    void createInterface();
    void destroyInterface();

private:
    QVector<UserInfo> m_users;
    QHash<uint, int> m_uidsToRows;
    QDBusInterface *m_dBusInterface;
    QDBusServiceWatcher *m_dBusWatcher;
};
#endif /* USERMODEL_H */
