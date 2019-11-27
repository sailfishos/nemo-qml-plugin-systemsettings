/*
 * Copyright (c) 2016 - 2019 Jolla Ltd.
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef SETTINGSVPNMODEL_H
#define SETTINGSVPNMODEL_H

#include <QObject>
#include <QSet>
#include <QDir>

#include <vpnconnection.h>
#include <vpnmodel.h>
#include <systemsettingsglobal.h>

class SYSTEMSETTINGS_EXPORT SettingsVpnModel : public VpnModel
{
    Q_OBJECT

    Q_PROPERTY(VpnConnection::ConnectionState bestState READ bestState NOTIFY bestStateChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(bool orderByConnected READ orderByConnected WRITE setOrderByConnected NOTIFY orderByConnectedChanged)

public:
    SettingsVpnModel(QObject* parent = nullptr);
    ~SettingsVpnModel() override;

    enum ItemRoles {
        ConnectedRole = VpnModel::VpnRole + 1
    };

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;

    VpnConnection::ConnectionState bestState() const;
    bool autoConnect() const;
    bool orderByConnected() const;
    void setOrderByConnected(bool orderByConnected);

    Q_INVOKABLE static bool isDefaultDomain(const QString &domain);
    Q_INVOKABLE void createConnection(const QVariantMap &properties);
    Q_INVOKABLE void modifyConnection(const QString &path, const QVariantMap &properties);
    Q_INVOKABLE void deleteConnection(const QString &path);

    Q_INVOKABLE void activateConnection(const QString &path);
    Q_INVOKABLE void deactivateConnection(const QString &path);

    Q_INVOKABLE QVariantMap connectionCredentials(const QString &path);
    Q_INVOKABLE void setConnectionCredentials(const QString &path, const QVariantMap &credentials);

    Q_INVOKABLE bool connectionCredentialsEnabled(const QString &path);
    Q_INVOKABLE void disableConnectionCredentials(const QString &path);

    Q_INVOKABLE QVariantMap connectionSettings(const QString &path);

    Q_INVOKABLE QVariantMap processProvisioningFile(const QString &path, const QString &type);

    Q_INVOKABLE VpnConnection *get(int index) const;

signals:
    void bestStateChanged();
    void autoConnectChanged();
    void connectionStateChanged(const QString &path, VpnConnection::ConnectionState state);
    void orderByConnectedChanged();

private:
    bool domainInUse(const QString &domain) const;
    QString createDefaultDomain() const;
    void reorderConnection(VpnConnection * conn);
    virtual void orderConnections(QVector<VpnConnection*> &connections) override;
    bool compareConnections(const VpnConnection *i, const VpnConnection *j);
    QVariantMap processOpenVpnProvisioningFile(QFile &provisioningFile);
    void updateBestState(VpnConnection::ConnectionState maxState);

private Q_SLOTS:
    void connectionAdded(const QString &path);
    void connectionRemoved(const QString &path);
    void connectionsRefreshed();
    void updatedConnectionPosition();
    void connectedChanged();
    void stateChanged();

private:
    class CredentialsRepository
    {
    public:
        CredentialsRepository(const QString &path);

        static QString locationForObjectPath(const QString &path);

        bool credentialsExist(const QString &location) const;

        bool storeCredentials(const QString &location, const QVariantMap &credentials);
        bool removeCredentials(const QString &location);

        QVariantMap credentials(const QString &location) const;

        static QByteArray encodeCredentials(const QVariantMap &credentials);
        static QVariantMap decodeCredentials(const QByteArray &encoded);

    private:
        QDir baseDir_;
    };

    CredentialsRepository credentials_;
    VpnConnection::ConnectionState bestState_;
    // True if there's one VPN that has autoConnect true
    bool autoConnect_;
    bool orderByConnected_;
    QString provisioningOutputPath_;
    QHash<int, QByteArray> roles;
};

#endif // SETTINGSVPNMODEL_H
