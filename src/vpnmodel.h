/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Matt Vogt <matthew.vogt@jollamobile.com>
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

#ifndef VPNMODEL_H
#define VPNMODEL_H

#include "connmanvpnproxy.h"

#include <systemsettingsglobal.h>
#include <objectlistmodel.h>

#include <QDir>
#include <QVariantMap>

class ConnmanServiceProxy;
class ConnmanVpnConnectionProxy;

class SYSTEMSETTINGS_EXPORT VpnConnection;

class SYSTEMSETTINGS_EXPORT VpnModel : public ObjectListModel
{
    Q_OBJECT

    Q_PROPERTY(int bestState READ bestState NOTIFY bestStateChanged)

public:
    enum ConnectionState {
        Idle,
        Failure,
        Configuration,
        Ready,
        Disconnect,
    };
    Q_ENUM(ConnectionState)

    explicit VpnModel(QObject *parent = 0);
    virtual ~VpnModel();

    int bestState() const;

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

    VpnConnection *connection(const QString &path) const;

signals:
    void bestStateChanged();
    void connectionStateChanged(const QString &path, int state);

private slots:
    void updatePendingDisconnectState();

private:
    void fetchVpnList();

    VpnConnection *newConnection(const QString &path);
    void updateConnection(VpnConnection *conn, const QVariantMap &properties);

    QVariantMap processOpenVpnProvisioningFile(QFile &provisioningFile);

    bool domainInUse(const QString &domain) const;
    QString createDefaultDomain() const;
    bool isDefaultDomain(const QString &domain) const;

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

    ConnmanVpnProxy connmanVpn_;
    QHash<QString, ConnmanVpnConnectionProxy *> connections_;
    QHash<QString, ConnmanServiceProxy *> vpnServices_;
    QSet<QString> defaultDomains_;
    QMap<QString, VpnConnection*> pendingDisconnects_;
    QString pendingConnect_;
    CredentialsRepository credentials_;
    QString provisioningOutputPath_;
    ConnectionState bestState_;
};

class SYSTEMSETTINGS_EXPORT VpnConnection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(QString domain READ domain WRITE setDomain NOTIFY domainChanged)
    Q_PROPERTY(QString networks READ networks WRITE setNetworks NOTIFY networksChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(bool storeCredentials READ storeCredentials WRITE setStoreCredentials NOTIFY storeCredentialsChanged)
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY typeChanged)
    Q_PROPERTY(bool immutable READ immutable WRITE setImmutable NOTIFY immutableChanged)
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY indexChanged)
    Q_PROPERTY(QVariantMap iPv4 READ iPv4 WRITE setIPv4 NOTIFY iPv4Changed)
    Q_PROPERTY(QVariantMap iPv6 READ iPv6 WRITE setIPv6 NOTIFY iPv6Changed)
    Q_PROPERTY(QStringList nameservers READ nameservers WRITE setNameservers NOTIFY nameserversChanged)
    Q_PROPERTY(QVariantList userRoutes READ userRoutes WRITE setUserRoutes NOTIFY userRoutesChanged)
    Q_PROPERTY(QVariantList serverRoutes READ serverRoutes WRITE setServerRoutes NOTIFY serverRoutesChanged)
    Q_PROPERTY(QVariantMap providerProperties READ providerProperties WRITE setProviderProperties NOTIFY providerPropertiesChanged)

public:
    VpnConnection(const QString &path);

    QString path() const { return path_; }

    QString name() const { return name_; }
    void setName(const QString &name) { updateMember(&VpnConnection::name_, name, &VpnConnection::nameChanged); }

    QString host() const { return host_; }
    void setHost(const QString &host) { updateMember(&VpnConnection::host_, host, &VpnConnection::hostChanged); }

    QString domain() const { return domain_; }
    void setDomain(const QString &domain) { updateMember(&VpnConnection::domain_, domain, &VpnConnection::domainChanged); }

    QString networks() const { return networks_; }
    void setNetworks(const QString &networks) { updateMember(&VpnConnection::networks_, networks, &VpnConnection::networksChanged); }

    bool autoConnect() const { return autoConnect_; }
    void setAutoConnect(bool autoConnect) { updateMember(&VpnConnection::autoConnect_, autoConnect, &VpnConnection::autoConnectChanged); }

    bool storeCredentials() const { return storeCredentials_; }
    void setStoreCredentials(bool storeCredentials) { updateMember(&VpnConnection::storeCredentials_, storeCredentials, &VpnConnection::storeCredentialsChanged); }

    int state() const { return state_; }
    void setState(int state) { updateMember(&VpnConnection::state_, state, &VpnConnection::stateChanged); }

    QString type() const { return type_; }
    void setType(const QString &type) { updateMember(&VpnConnection::type_, type, &VpnConnection::typeChanged); }

    bool immutable() const { return immutable_; }
    void setImmutable(bool immutable) { updateMember(&VpnConnection::immutable_, immutable, &VpnConnection::immutableChanged); }

    int index() const { return index_; }
    void setIndex(int index) { updateMember(&VpnConnection::index_, index, &VpnConnection::indexChanged); }

    QVariantMap iPv4() const { return ipv4_; }
    void setIPv4(const QVariantMap &ipv4) { updateMember(&VpnConnection::ipv4_, ipv4, &VpnConnection::iPv4Changed); }

    QVariantMap iPv6() const { return ipv6_; }
    void setIPv6(const QVariantMap &ipv6) { updateMember(&VpnConnection::ipv6_, ipv6, &VpnConnection::iPv6Changed); }

    QStringList nameservers() const { return nameservers_; }
    void setNameservers(const QStringList &nameservers) { updateMember(&VpnConnection::nameservers_, nameservers, &VpnConnection::nameserversChanged); }

    QVariantList userRoutes() const { return userRoutes_; }
    void setUserRoutes(const QVariantList &userRoutes) { updateMember(&VpnConnection::userRoutes_, userRoutes, &VpnConnection::userRoutesChanged); }

    QVariantList serverRoutes() const { return serverRoutes_; }
    void setServerRoutes(const QVariantList &serverRoutes) { updateMember(&VpnConnection::serverRoutes_, serverRoutes, &VpnConnection::serverRoutesChanged); }

    QVariantMap providerProperties() const { return providerProperties_; }
    void setProviderProperties(const QVariantMap  providerProperties) { updateMember(&VpnConnection::providerProperties_, providerProperties, &VpnConnection::providerPropertiesChanged); }

signals:
    void nameChanged();
    void stateChanged();
    void typeChanged();
    void hostChanged();
    void domainChanged();
    void networksChanged();
    void autoConnectChanged();
    void storeCredentialsChanged();
    void immutableChanged();
    void indexChanged();
    void iPv4Changed();
    void iPv6Changed();
    void nameserversChanged();
    void userRoutesChanged();
    void serverRoutesChanged();
    void providerPropertiesChanged();

private:
    template<typename T, typename V>
    bool updateMember(T VpnConnection::*member, const V &value, void (VpnConnection::*func)()) {
        if (this->*member != value) {
            this->*member = value;
            emit (this->*func)();
            return true;
        }
        return false;
    }

    QString path_;
    QString name_;
    int state_;
    QString type_;
    QString host_;
    QString domain_;
    QString networks_;
    bool autoConnect_;
    bool storeCredentials_;
    bool immutable_;
    int index_;
    QVariantMap ipv4_;
    QVariantMap ipv6_;
    QStringList nameservers_;
    QVariantList userRoutes_;
    QVariantList serverRoutes_;
    QVariantMap providerProperties_;
};

Q_DECLARE_METATYPE(VpnModel::ConnectionState)

#endif
