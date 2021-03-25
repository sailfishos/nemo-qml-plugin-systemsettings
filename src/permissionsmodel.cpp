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

#include <MDesktopEntry>
#include <MPermission>
#include "permissionsmodel.h"

namespace {

bool permissionLessThan(const MPermission &p1, const MPermission &p2)
{
    return (p1.description().localeAwareCompare(p2.description()) < 0);
}

}

PermissionsModel::PermissionsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

PermissionsModel::~PermissionsModel()
{
}

QString PermissionsModel::desktopFile() const
{
    return m_desktopFile;
}

void PermissionsModel::setDesktopFile(QString file)
{
    if (m_desktopFile != file) {
        m_desktopFile = file;
        loadPermissions();
        emit desktopFileChanged();
        emit countChanged();
    }
}

QHash<int, QByteArray> PermissionsModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { Qt::DisplayRole, "display" },
        { DescriptionRole, "description" },
        { LongDescriptionRole, "longDescription" },
        { NameRole, "name" },
    };
    return roles;
}

int PermissionsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_permissions.count();
}

QVariant PermissionsModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount() || index.column() != 0)
        return QVariant();

    switch (role) {
    case Qt::DisplayRole:
    case DescriptionRole:
        return m_permissions.at(index.row()).description();
    case LongDescriptionRole:
        return m_permissions.at(index.row()).longDescription();
    case NameRole:
        return m_permissions.at(index.row()).name();
    default:
        return QVariant();
    }
}

void PermissionsModel::loadPermissions()
{
    if (!m_permissions.isEmpty()) {
        beginRemoveRows(QModelIndex(), 0, m_permissions.length() - 1);
        m_permissions.clear();
        endRemoveRows();
    }

    MDesktopEntry entry(m_desktopFile);
    if (entry.isValid()) {
        auto permissions = MPermission::fromDesktopEntry(entry);
        if (!permissions.isEmpty()) {
            beginInsertRows(QModelIndex(), 0, permissions.length() - 1);
            m_permissions.swap(permissions);
            std::sort(m_permissions.begin(), m_permissions.end(), permissionLessThan);
            endInsertRows();
        }
    }
}
