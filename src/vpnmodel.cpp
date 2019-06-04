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

#include "vpnmodel.h"
#include "logging_p.h"
#include "connmanvpnconnectionproxy.h"
#include "connmanserviceproxy.h"

#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QRegularExpression>

#include <nemo-dbus/dbus.h>

namespace {

const auto defaultDomain = QStringLiteral("sailfishos.org");
const auto legacyDefaultDomain(QStringLiteral("merproject.org"));
const auto connmanService = QStringLiteral("net.connman");
const auto connmanVpnService = QStringLiteral("net.connman.vpn");
const auto autoConnectKey = QStringLiteral("AutoConnect");

QString vpnServicePath(QString connectionPath)
{
    return QString("/net/connman/service/vpn_%1").arg(connectionPath.section("/", 5));
}

// Conversion to/from DBus/QML
QHash<QString, QList<QPair<QVariant, QVariant> > > propertyConversions()
{
    QHash<QString, QList<QPair<QVariant, QVariant> > > rv;

    QList<QPair<QVariant, QVariant> > states;
    states.push_back(qMakePair(QVariant::fromValue(QString("idle")), QVariant::fromValue(static_cast<int>(VpnModel::Idle))));
    states.push_back(qMakePair(QVariant::fromValue(QString("failure")), QVariant::fromValue(static_cast<int>(VpnModel::Failure))));
    states.push_back(qMakePair(QVariant::fromValue(QString("configuration")), QVariant::fromValue(static_cast<int>(VpnModel::Configuration))));
    states.push_back(qMakePair(QVariant::fromValue(QString("ready")), QVariant::fromValue(static_cast<int>(VpnModel::Ready))));
    states.push_back(qMakePair(QVariant::fromValue(QString("disconnect")), QVariant::fromValue(static_cast<int>(VpnModel::Disconnect))));
    rv.insert(QString("state"), states);

    return rv;
}

QVariant convertValue(const QString &key, const QVariant &value, bool toDBus)
{
    static const QHash<QString, QList<QPair<QVariant, QVariant> > > conversions(propertyConversions());

    auto it = conversions.find(key.toLower());
    if (it != conversions.end()) {
        const QList<QPair<QVariant, QVariant> > &list(it.value());
        auto lit = std::find_if(list.cbegin(), list.cend(), [value, toDBus](const QPair<QVariant, QVariant> &pair) { return value == (toDBus ? pair.second : pair.first); });
        if (lit != list.end()) {
            return toDBus ? (*lit).first : (*lit).second;
        } else {
            qCWarning(lcVpnLog) << "No conversion found for" << (toDBus ? "QML" : "DBus") << "value:" << value << key;
        }
    }

    return value;
}

QVariant convertToQml(const QString &key, const QVariant &value)
{
    return convertValue(key, value, false);
}

QVariant convertToDBus(const QString &key, const QVariant &value)
{
    return convertValue(key, value, true);
}

QVariantMap propertiesToDBus(const QVariantMap &fromQml)
{
    QVariantMap rv;

    for (QVariantMap::const_iterator it = fromQml.cbegin(), end = fromQml.cend(); it != end; ++it) {
        QString key(it.key());
        QVariant value(it.value());

        if (key == QStringLiteral("providerProperties")) {
            const QVariantMap providerProperties(value.value<QVariantMap>());
            for (QVariantMap::const_iterator pit = providerProperties.cbegin(), pend = providerProperties.cend(); pit != pend; ++pit) {
                rv.insert(pit.key(), pit.value());
            }
            continue;
        }

        // The DBus properties are capitalized
        QChar &initial(*key.begin());
        initial = initial.toUpper();

        rv.insert(key, convertToDBus(key, value));
    }

    return rv;
}

template<typename T>
QVariant extract(const QDBusArgument &arg)
{
    T rv;
    arg >> rv;
    return QVariant::fromValue(rv);
}

template<typename T>
QVariant extractArray(const QDBusArgument &arg)
{
    QVariantList rv;

    arg.beginArray();
    while (!arg.atEnd()) {
        rv.append(extract<T>(arg));
    }
    arg.endArray();

    return QVariant::fromValue(rv);
}

QVariantMap propertiesToQml(const QVariantMap &fromDBus)
{
    QVariantMap rv;

    QVariantMap providerProperties;

    for (QVariantMap::const_iterator it = fromDBus.cbegin(), end = fromDBus.cend(); it != end; ++it) {
        QString key(it.key());
        QVariant value(it.value());

        if (key.indexOf(QChar('.')) != -1) {
            providerProperties.insert(key, value);
            continue;
        }

        // QML properties must be lowercased
        QChar &initial(*key.begin());
        initial = initial.toLower();

        // Some properties must be extracted manually
        if (key == QStringLiteral("iPv4") ||
            key == QStringLiteral("iPv6")) {
            value = extract<QVariantMap>(value.value<QDBusArgument>());
        } else if (key == QStringLiteral("serverRoutes") ||
                   key == QStringLiteral("userRoutes")) {
            value = extractArray<QVariantMap>(value.value<QDBusArgument>());
        }

        rv.insert(key, convertToQml(key, value));
    }

    if (!providerProperties.isEmpty()) {
        rv.insert(QStringLiteral("providerProperties"), QVariant::fromValue(providerProperties));
    }

    return rv;
}

int numericValue(VpnModel::ConnectionState state)
{
    return (state == VpnModel::Ready ? 3 : (state == VpnModel::Configuration ? 2 : (state == VpnModel::Failure ? 1 : 0)));
}

} // end anonymous namespace

VpnModel::CredentialsRepository::CredentialsRepository(const QString &path)
    : baseDir_(path)
{
    if (!baseDir_.exists() && !baseDir_.mkpath(path)) {
        qCWarning(lcVpnLog) << "Unable to create base directory for VPN credentials:" << path;
    }
}

QString VpnModel::CredentialsRepository::locationForObjectPath(const QString &path)
{
    int index = path.lastIndexOf(QChar('/'));
    if (index != -1) {
        return path.mid(index + 1);
    }

    return QString();
}

bool VpnModel::CredentialsRepository::credentialsExist(const QString &location) const
{
    // Test the FS, as another process may store/remove the credentials
    return baseDir_.exists(location);
}

bool VpnModel::CredentialsRepository::storeCredentials(const QString &location, const QVariantMap &credentials)
{
    QFile credentialsFile(baseDir_.absoluteFilePath(location));
    if (!credentialsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcVpnLog) << "Unable to write credentials file:" << credentialsFile.fileName();
        return false;
    } else {
        credentialsFile.write(encodeCredentials(credentials));
        credentialsFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther | QFileDevice::WriteOther);
        credentialsFile.close();
    }

    return true;
}

bool VpnModel::CredentialsRepository::removeCredentials(const QString &location)
{
    if (baseDir_.exists(location)) {
        if (!baseDir_.remove(location)) {
            qCWarning(lcVpnLog) << "Unable to delete credentials file:" << location;
            return false;
        }
    }

    return true;
}

QVariantMap VpnModel::CredentialsRepository::credentials(const QString &location) const
{
    QVariantMap rv;

    QFile credentialsFile(baseDir_.absoluteFilePath(location));
    if (!credentialsFile.open(QIODevice::ReadOnly)) {
        qCWarning(lcVpnLog) << "Unable to read credentials file:" << credentialsFile.fileName();
    } else {
        const QByteArray encoded = credentialsFile.readAll();
        credentialsFile.close();

        rv = decodeCredentials(encoded);
    }

    return rv;
}

QByteArray VpnModel::CredentialsRepository::encodeCredentials(const QVariantMap &credentials)
{
    // We can't store these values securely, but we may as well encode them to protect from grep, at least...
    QByteArray encoded;

    QDataStream os(&encoded, QIODevice::WriteOnly);
    os.setVersion(QDataStream::Qt_5_6);

    const quint32 version = 1u;
    os << version;

    const quint32 items = credentials.size();
    os << items;

    for (auto it = credentials.cbegin(), end = credentials.cend(); it != end; ++it) {
        os << it.key();
        os << it.value().toString();
    }

    return encoded.toBase64();
}

QVariantMap VpnModel::CredentialsRepository::decodeCredentials(const QByteArray &encoded)
{
    QVariantMap rv;

    QByteArray decoded(QByteArray::fromBase64(encoded));

    QDataStream is(decoded);
    is.setVersion(QDataStream::Qt_5_6);

    quint32 version;
    is >> version;

    if (version != 1u) {
        qCWarning(lcVpnLog) << "Invalid version for stored credentials:" << version;
    } else {
        quint32 items;
        is >> items;

        for (quint32 i = 0; i < items; ++i) {
            QString key, value;
            is >> key;
            is >> value;
            rv.insert(key, QVariant::fromValue(value));
        }
    }

    return rv;
}


VpnModel::VpnModel(QObject *parent)
    : ObjectListModel(parent, true, false)
    , connmanVpn_(connmanVpnService, "/", QDBusConnection::systemBus(), this)
    , credentials_(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/system/privileged/vpn-data"))
    , provisioningOutputPath_(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/system/privileged/vpn-provisioning"))
    , bestState_(VpnModel::Idle)
    , autoConnect_(false)
    , orderByConnected_(false)
{
    qDBusRegisterMetaType<PathProperties>();
    qDBusRegisterMetaType<PathPropertiesArray>();

    connect(&connmanVpn_, &ConnmanVpnProxy::ConnectionAdded, [this](const QDBusObjectPath &objectPath, const QVariantMap &properties) {
        const QString path(objectPath.path());
        VpnConnection *conn = connection(path);
        if (!conn) {
            qCDebug(lcVpnLog) << "Adding connection:" << path;
            conn = newConnection(path);
        }

        QVariantMap qmlProperties(propertiesToQml(properties));
        qmlProperties.insert(QStringLiteral("storeCredentials"), credentials_.credentialsExist(CredentialsRepository::locationForObjectPath(path)));
        updateConnection(conn, qmlProperties);
    });

    connect(&connmanVpn_, &ConnmanVpnProxy::ConnectionRemoved, [this](const QDBusObjectPath &objectPath) {
        const QString path(objectPath.path());
        if (VpnConnection *conn = connection(path)) {
            qCDebug(lcVpnLog) << "Removing obsolete connection:" << path;
            removeItem(conn);
            delete conn;
        } else {
            qCWarning(lcVpnLog) << "Unable to remove unknown connection:" << path;
        }

        // Remove the proxy if present
        auto it = connections_.find(path);
        if (it != connections_.end()) {
            ConnmanVpnConnectionProxy *proxy(*it);
            connections_.erase(it);
            delete proxy;
        }

        auto vpnServiceIterator = vpnServices_.find(path);
        if (vpnServiceIterator != vpnServices_.end()) {
            ConnmanServiceProxy *proxy(*vpnServiceIterator);
            vpnServices_.erase(vpnServiceIterator);
            delete proxy;
        }
    });

    // If connman-vpn restarts, we need to discard and re-read the state
    QDBusServiceWatcher *watcher = new QDBusServiceWatcher(connmanVpnService, QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &) {
        for (int i = 0, n = count(); i < n; ++i) {
            get(i)->deleteLater();
        }
        clear();
        setPopulated(false);

        qDeleteAll(connections_);
        connections_.clear();

        qDeleteAll(vpnServices_);
        vpnServices_.clear();
    });
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString &) {
        fetchVpnList();
    });

    fetchVpnList();
}

VpnModel::~VpnModel()
{
    deleteAll();
}

int VpnModel::bestState() const
{
    return static_cast<int>(bestState_);
}

bool VpnModel::autoConnect() const
{
    return autoConnect_;
}

bool VpnModel::orderByConnected() const
{
    return orderByConnected_;
}

void VpnModel::setOrderByConnected(bool orderByConnected)
{
    if (orderByConnected != orderByConnected_) {
        orderByConnected_ = orderByConnected;

        // Update the ordering; only the connected connections need to move
        // In practice only one VPN can be connected, so full sort is overkill
        const int itemCount(count());
        for (int index = 0; index < itemCount; ++index) {
            VpnConnection *conn = get<VpnConnection>(index);
            if (conn->connected()) {
                reorderConnection(conn);
            }
        }

        emit orderByConnectedChanged();
    }
}

void VpnModel::createConnection(const QVariantMap &createProperties)
{
    const QString path(createProperties.value(QString("path")).toString());
    if (path.isEmpty()) {
        const QString host(createProperties.value(QString("host")).toString());
        const QString name(createProperties.value(QString("name")).toString());

        if (!host.isEmpty() && !name.isEmpty()) {
            // Connman requires a domain value, but doesn't seem to use it...
            QVariantMap properties(createProperties);
            const QString domain(properties.value(QString("domain")).toString());
            if (domain.isEmpty()) {
                properties.insert(QString("domain"), QVariant::fromValue(createDefaultDomain()));
            }

            QDBusPendingCall call = connmanVpn_.Create(propertiesToDBus(properties));

            QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                watcher->deleteLater();

                if (reply.isError()) {
                    qCWarning(lcVpnLog) << "Unable to create Connman VPN connection:" << reply.error().message();
                } else {
                    const QDBusObjectPath &objectPath(reply.value());
                    qCWarning(lcVpnLog) << "Created VPN connection:" << objectPath.path();
                }
            });
        } else {
            qCWarning(lcVpnLog) << "Unable to create VPN connection without domain, host and name properties";
        }
    } else {
        qCWarning(lcVpnLog) << "Unable to create VPN connection with pre-existing path:" << path;
    }
}

void VpnModel::modifyConnection(const QString &path, const QVariantMap &properties)
{
    auto it = connections_.find(path);
    if (it != connections_.end()) {
        // ConnmanVpnConnectionProxy provides the SetProperty interface to modify a connection,
        // but as far as I can tell, the only way to cause Connman to store the configuration to
        // disk is to create a new connection...  Work around this by removing the existing
        // connection and recreating it with the updated properties.
        qCWarning(lcVpnLog) << "Updating VPN connection for modification:" << path;

        // Remove properties that connman doesn't know about
        QVariantMap updatedProperties(properties);
        updatedProperties.remove(QString("path"));
        updatedProperties.remove(QString("state"));
        updatedProperties.remove(QString("index"));
        updatedProperties.remove(QString("immutable"));
        updatedProperties.remove(QString("storeCredentials"));

        const QString domain(updatedProperties.value(QString("domain")).toString());
        if (domain.isEmpty()) {
            updatedProperties.insert(QString("domain"), QVariant::fromValue(createDefaultDomain()));
        }

        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool couldStoreCredentials(credentials_.credentialsExist(location));
        const bool canStoreCredentials(properties.value(QString("storeCredentials")).toBool());

        ConnmanVpnConnectionProxy *proxy(*it);
        QVariantMap dbusProps = propertiesToDBus(updatedProperties);
        for (QMap<QString, QVariant>::const_iterator i = dbusProps.constBegin(); i != dbusProps.constEnd(); ++i) {
            proxy->SetProperty(i.key(), QDBusVariant(i.value()));
        }

        if (canStoreCredentials != couldStoreCredentials) {
            if (canStoreCredentials) {
                credentials_.storeCredentials(location, QVariantMap());
            } else {
                credentials_.removeCredentials(location);
            }
        }


    } else {
        qCWarning(lcVpnLog) << "Unable to update unknown VPN connection:" << path;
    }
}

void VpnModel::deleteConnection(const QString &path)
{
    if (VpnConnection *conn = connection(path)) {
        if (conn->state() == VpnModel::Ready || conn->state() == VpnModel::Configuration) {
            ConnmanServiceProxy* proxy = vpnServices_.value(path);
            if (proxy) {
                proxy->SetProperty(autoConnectKey, QDBusVariant(false));
                QDBusPendingCall call = proxy->Disconnect();
                QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
                connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
                    watcher->deleteLater();
                    // Regardless of reply status let's remove it.
                    deleteConnection(path);
                });
            }
        } else {

            // Remove cached credentials
            const QString location(CredentialsRepository::locationForObjectPath(path));
            if (credentials_.credentialsExist(location)) {
                credentials_.removeCredentials(location);
            }

            // Remove provisioned files
            if (conn->type() == QStringLiteral("openvpn")) {
                QVariantMap providerProperties = conn->providerProperties();
                QStringList fileProperties;
                fileProperties << QStringLiteral("OpenVPN.Cert") << QStringLiteral("OpenVPN.Key") << QStringLiteral("OpenVPN.CACert") << QStringLiteral("OpenVPN.ConfigFile");
                for (const QString property : fileProperties) {
                    const QString filename = providerProperties.value(property).toString();

                    // Check if the file has been provisioned
                    if (filename.contains(provisioningOutputPath_)) {
                        int timesUsed = 0;

                        // Check the same file is not used by other connections
                        for (int i = 0, n = count(); i < n; ++i) {
                            VpnConnection *c = qobject_cast<VpnConnection *>(get(i));
                            if (filename == c->providerProperties().value(property).toString()) {
                                timesUsed++;
                                if (timesUsed > 1) {
                                    break;
                                }
                            }
                        }

                        if (timesUsed > 1) {
                            continue;
                        }

                        if (!QFile::remove(filename)) {
                            qCWarning(lcVpnLog)  << "Failed to remove provisioning file" << filename;
                        }
                    }
                }
            }

            QDBusPendingCall call = connmanVpn_.Remove(QDBusObjectPath(path));
            QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<void> reply = *watcher;
                watcher->deleteLater();
                if (reply.isError()) {
                    qCWarning(lcVpnLog) << "Unable to delete Connman VPN connection:" << path << ":" << reply.error().message();
                } else {
                    qCWarning(lcVpnLog) << "Deleted connection:" << path;
                }
            });
        }
    } else {
        qCWarning(lcVpnLog) << "Unable to delete unknown connection:" << path;
    }
}

void VpnModel::activateConnection(const QString &path)
{
    qCInfo(lcVpnLog) << "Connect" << path;
    for (int i = 0, n = count(); i < n; ++i) {
        VpnConnection *connection = qobject_cast<VpnConnection *>(get(i));
        QString otherPath = connection->path();
        if (otherPath != path && (connection->state() == VpnModel::Ready ||
                                  connection->state() == VpnModel::Configuration)) {
            deactivateConnection(otherPath);
            qCDebug(lcVpnLog) << "Adding pending vpn disconnect" << otherPath << connection->state() << "when connecting to vpn";
        }
    }

    qCDebug(lcVpnLog) << "About to connect path:" << path;

    ConnmanServiceProxy* proxy = vpnServices_.value(path);
    if (proxy) {
        QDBusPendingCall call = proxy->Connect();
        qCDebug(lcVpnLog) << "Connect to vpn" << path;

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<void> reply = *watcher;
            watcher->deleteLater();

            if (reply.isError()) {
                qCWarning(lcVpnLog) << "Unable to activate Connman VPN connection:" << path << ":" << reply.error().message();
            }
        });
    } else {
        qCWarning(lcVpnLog) << "Unable to activate VPN connection without proxy:" << path;
    }
}

void VpnModel::deactivateConnection(const QString &path)
{
    qCInfo(lcVpnLog) << "Disconnect" << path;
    ConnmanServiceProxy* proxy = vpnServices_.value(path);
    if (proxy) {
        QDBusPendingCall call = proxy->Disconnect();
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<void> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                qCWarning(lcVpnLog) << "Unable to deactivate Connman VPN connection:" << path << ":" << reply.error().message();
            }
        });
    } else {
        qCWarning(lcVpnLog) << "Unable to deactivate VPN connection without proxy:" << path;
    }
}

QVariantMap VpnModel::connectionCredentials(const QString &path)
{
    QVariantMap rv;

    if (VpnConnection *conn = connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool enabled(credentials_.credentialsExist(location));

        if (enabled) {
            rv = credentials_.credentials(location);
        } else {
            qCWarning(lcVpnLog) << "VPN does not permit credentials storage:" << path;
        }

        if (conn->storeCredentials() != enabled) {
            conn->setStoreCredentials(enabled);
            itemChanged(conn);
        }
    } else {
        qCWarning(lcVpnLog) << "Unable to return credentials for unknown VPN connection:" << path;
    }

    return rv;
}

void VpnModel::setConnectionCredentials(const QString &path, const QVariantMap &credentials)
{
    if (VpnConnection *conn = connection(path)) {
        credentials_.storeCredentials(CredentialsRepository::locationForObjectPath(path), credentials);

        if (!conn->storeCredentials()) {
            conn->setStoreCredentials(true);
        }
        itemChanged(conn);
    } else {
        qCWarning(lcVpnLog) << "Unable to set credentials for unknown VPN connection:" << path;
    }
}

bool VpnModel::connectionCredentialsEnabled(const QString &path)
{
    if (VpnConnection *conn = connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool enabled(credentials_.credentialsExist(location));

        if (conn->storeCredentials() != enabled) {
            conn->setStoreCredentials(enabled);
            itemChanged(conn);
        }
        return enabled;
    } else {
        qCWarning(lcVpnLog) << "Unable to test credentials storage for unknown VPN connection:" << path;
    }

    return false;
}

void VpnModel::disableConnectionCredentials(const QString &path)
{
    if (VpnConnection *conn = connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        if (credentials_.credentialsExist(location)) {
            credentials_.removeCredentials(location);
        }

        if (conn->storeCredentials()) {
            conn->setStoreCredentials(false);
        }
        itemChanged(conn);
    } else {
        qCWarning(lcVpnLog) << "Unable to set automatic connection for unknown VPN connection:" << path;
    }
}

QVariantMap VpnModel::connectionSettings(const QString &path)
{
    QVariantMap rv;
    if (VpnConnection *conn = connection(path)) {
        // Check if the credentials storage has been changed
        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool enabled(credentials_.credentialsExist(location));
        if (conn->storeCredentials() != enabled) {
            conn->setStoreCredentials(enabled);
            itemChanged(conn);
        }

        rv = itemRoles(conn);
    }
    return rv;
}

QVariantMap VpnModel::processProvisioningFile(const QString &path, const QString &type)
{
    QVariantMap rv;

    QFile provisioningFile(path);
    if (provisioningFile.open(QIODevice::ReadOnly)) {
        if (type == QString("openvpn")) {
            rv = processOpenVpnProvisioningFile(provisioningFile);
        } else {
            qCWarning(lcVpnLog) << "Provisioning not currently supported for VPN type:" << type;
        }
    } else {
        qCWarning(lcVpnLog) << "Unable to open provisioning file:" << path;
    }

    return rv;
}

void VpnModel::fetchVpnList()
{
    QDBusPendingCall call = connmanVpn_.GetConnections();

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<PathPropertiesArray> reply = *watcher;
        watcher->deleteLater();

        if (reply.isError()) {
            qCWarning(lcVpnLog) << "Unable to fetch Connman VPN connections:" << reply.error().message();
        } else {
            const PathPropertiesArray &connections(reply.value());

            for (const PathProperties &connection : connections) {
                const QString &path(connection.first.path());
                const QVariantMap &properties(connection.second);

                QVariantMap qmlProperties(propertiesToQml(properties));
                qmlProperties.insert(QStringLiteral("storeCredentials"), credentials_.credentialsExist(CredentialsRepository::locationForObjectPath(path)));

                VpnConnection *conn = newConnection(path);
                updateConnection(conn, qmlProperties);
            }
        }

        setPopulated(true);
    });
}

VpnConnection *VpnModel::connection(const QString &path) const
{
    for (int i = 0, n = count(); i < n; ++i) {
        VpnConnection *connection = qobject_cast<VpnConnection *>(get(i));
        if (connection->path() == path) {
            return connection;
        }
    }

    return nullptr;
}

VpnConnection *VpnModel::newConnection(const QString &path)
{
    VpnConnection *conn = new VpnConnection(path);
    appendItem(conn);

    // Create a vpn and a connman service proxies for this connection
    ConnmanVpnConnectionProxy *proxy = new ConnmanVpnConnectionProxy(connmanVpnService, path, QDBusConnection::systemBus(), nullptr);
    ConnmanServiceProxy *serviceProxy = new ConnmanServiceProxy(connmanService, vpnServicePath(path), QDBusConnection::systemBus(), nullptr);

    connections_.insert(path, proxy);
    vpnServices_.insert(path, serviceProxy);

    QDBusPendingCall servicePropertiesCall = serviceProxy->GetProperties();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(servicePropertiesCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, conn, path](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<> reply = *watcher;
        if (reply.isFinished() && reply.isValid()) {
            QDBusMessage message = reply.reply();
            QVariantMap properties = NemoDBus::demarshallArgument<QVariantMap>(message.arguments().value(0));
            bool autoConnect = properties.value(autoConnectKey).toBool();
            properties.clear();
            properties.insert(autoConnectKey, autoConnect);
            qCInfo(lcVpnLog) << "Initial VPN service properties:" << properties << path << conn->name();
            updateConnection(conn, propertiesToQml(properties));
        } else {
            qCDebug(lcVpnLog) << "Error :" << path << ":" << reply.error().message();
        }

        watcher->deleteLater();
    });

    connect(proxy, &ConnmanVpnConnectionProxy::PropertyChanged, this, [this, conn](const QString &name, const QDBusVariant &value) {
        ConnmanVpnConnectionProxy *proxy = qobject_cast<ConnmanVpnConnectionProxy *>(sender());
        QVariantMap properties;
        qCInfo(lcVpnLog) << "VPN connection property changed:" << name << value.variant() << proxy->path() << conn->name();
        properties.insert(name, value.variant());
        updateConnection(conn, propertiesToQml(properties));
    });

    connect(serviceProxy, &ConnmanServiceProxy::PropertyChanged, this, [this, conn](const QString &name, const QDBusVariant &value) {
        ConnmanServiceProxy *proxy = qobject_cast<ConnmanServiceProxy *>(sender());
        qCInfo(lcVpnLog) << "VPN service property changed:" << name << value.variant() << proxy->path() << conn->name();
        if (name == autoConnectKey) {
            QVariantMap properties;
            properties.insert(name, value.variant());
            updateConnection(conn, propertiesToQml(properties));
        }
    });

    connect(conn, &VpnConnection::autoConnectChanged, conn, [this, conn]() {
        qCInfo(lcVpnLog) << "VPN autoconnect changed:" << conn->name() << conn->autoConnect();
        ConnmanServiceProxy* proxy = vpnServices_.value(conn->path());
        if (proxy) {
            proxy->SetProperty(autoConnectKey, QDBusVariant(conn->autoConnect()));
        }
    });

    return conn;
}

void VpnModel::updateConnection(VpnConnection *conn, const QVariantMap &updateProperties)
{
    QVariantMap properties(updateProperties);

    // If providerProperties have been modified, merge them with existing values
    auto ppit = properties.find(QStringLiteral("providerProperties"));
    if (ppit != properties.end()) {
        QVariantMap existingProperties = conn->providerProperties();

        QVariantMap updated = (*ppit).value<QVariantMap>();
        for (QVariantMap::const_iterator pit = updated.cbegin(), pend = updated.cend(); pit != pend; ++pit) {
            existingProperties.insert(pit.key(), pit.value());
        }

        *ppit = QVariant::fromValue(existingProperties);
    }

    ppit = properties.find(QStringLiteral("domain"));
    if (ppit != properties.end()) {
        QString domain = (*ppit).value<QString>();
        if (isDefaultDomain(domain)) {
            // Default domains are dropped from the model data but
            // let's track what default domains we have seen.
            defaultDomains_.insert(domain);
            properties.erase(ppit);
        }
    }

    int oldState(conn->state());
    bool connectionChanged = false;

    if (updateItem(conn, properties)) {
        itemChanged(conn);

        const int itemCount(count());

        if (conn->state() != oldState) {
            emit connectionStateChanged(conn->path(), static_cast<int>(conn->state()));
            if ((conn->state() == VpnModel::Ready) != (oldState == VpnModel::Ready)) {
                emit conn->connectedChanged();
                connectionChanged = true;
            }

            // Check to see if the best state has changed
            ConnectionState maxState = Idle;
            for (int i = 0; i < itemCount; ++i) {
                ConnectionState state(static_cast<ConnectionState>(get<VpnConnection>(i)->state()));
                if (numericValue(state) > numericValue(maxState)) {
                    maxState = state;
                }
            }
            if (bestState_ != maxState) {
                bestState_ = maxState;
                emit bestStateChanged();
            }
        }

        // Keep the items sorted by name and possibly connected status. So sort
        // only when the connection status has changed, or the updateProperties
        // map contains a name i.e. not when "autoConnect" changes. In practice
        // this means that if orderByConnected_ is false then sorting is only
        // allowed when a VPN is created. When modifying name of a VPN, the VPN
        // will be first removed and then recreated.
        if (updateProperties.contains(QStringLiteral("name")) || (orderByConnected_ && connectionChanged)) {
            reorderConnection(conn);
        }
    }

    bool autoConnect = false;
    for (int i = 0; i < count(); ++i) {
        autoConnect = autoConnect || get<VpnConnection>(i)->autoConnect();
    }

    if (autoConnect_ != autoConnect) {
        autoConnect_ = autoConnect;
        autoConnectChanged();
    }
}

void VpnModel::reorderConnection(VpnConnection * conn)
{
    const int itemCount(count());

    if (itemCount > 1) {
        int index = 0;
        for ( ; index < itemCount; ++index) {
            const VpnConnection *existing = get<VpnConnection>(index);
            // Scenario 1 orderByConnected == true: order first by connected, second by name
            // Scenario 2 orderByConnected == false: order only by name
            if ((orderByConnected_ && (existing->connected() < conn->connected()))
                    || ((!orderByConnected_ || (existing->connected() == conn->connected()))
                        && (existing->name() > conn->name()))) {
                break;
            }
        }
        const int currentIndex = indexOf(conn);
        if (index != currentIndex && (index - 1) != currentIndex) {
            moveItem(currentIndex, (currentIndex < index ? (index - 1) : index));
        }
    }
}

QVariantMap VpnModel::processOpenVpnProvisioningFile(QFile &provisioningFile)
{
    QVariantMap rv;

    QString embeddedMarker;
    QString embeddedContent;
    QStringList extraOptions;

    const QRegularExpression commentLeader(QStringLiteral("^\\s*(?:\\#|\\;)"));
    const QRegularExpression embeddedLeader(QStringLiteral("^\\s*<([^\\/>]+)>"));
    const QRegularExpression embeddedTrailer(QStringLiteral("^\\s*<\\/([^\\/>]+)>"));
    const QRegularExpression whitespace(QStringLiteral("\\s"));

    auto normaliseProtocol = [](const QString &proto) {
        if (proto == QStringLiteral("tcp")) {
            // 'tcp' is an undocumented option, which is interpreted by openvpn as 'tcp-client'
            return QStringLiteral("tcp-client");
        }
        return proto;
    };

    QTextStream is(&provisioningFile);
    while (!is.atEnd()) {
        QString line(is.readLine());

        QRegularExpressionMatch match;
        if (line.contains(commentLeader)) {
            // Skip
        } else if (line.contains(embeddedLeader, &match)) {
            embeddedMarker = match.captured(1);
            if (embeddedMarker.isEmpty()) {
                qCWarning(lcVpnLog) << "Invalid embedded content";
            }
        } else if (line.contains(embeddedTrailer, &match)) {
            const QString marker = match.captured(1);
            if (marker != embeddedMarker) {
                qCWarning(lcVpnLog) << "Invalid embedded content:" << marker << "!=" << embeddedMarker;
            } else {
                if (embeddedContent.isEmpty()) {
                    qCWarning(lcVpnLog) << "Ignoring empty embedded content:" << embeddedMarker;
                } else {
                    if (embeddedMarker == QStringLiteral("connection")) {
                        // Special case: not embedded content, but a <connection> structure - pass through as an extra option
                        extraOptions.append(QStringLiteral("<connection>\n") + embeddedContent + QStringLiteral("</connection>"));
                    } else {
                        // Embedded content
                        QDir outputDir(provisioningOutputPath_);
                        if (!outputDir.exists() && !outputDir.mkpath(provisioningOutputPath_)) {
                            qCWarning(lcVpnLog) << "Unable to create base directory for VPN provisioning content:" << provisioningOutputPath_;
                        } else {
                            // Name the file according to content
                            QCryptographicHash hash(QCryptographicHash::Sha1);
                            hash.addData(embeddedContent.toUtf8());

                            const QString outputFileName(QString(hash.result().toHex()) + QChar('.') + embeddedMarker);
                            QFile outputFile(outputDir.absoluteFilePath(outputFileName));
                            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                qCWarning(lcVpnLog) << "Unable to write VPN provisioning content file:" << outputFile.fileName();
                            } else {
                                QTextStream os(&outputFile);
                                os << embeddedContent;

                                // Add the file to the configuration
                                if (embeddedMarker == QStringLiteral("ca")) {
                                    rv.insert(QStringLiteral("OpenVPN.CACert"), outputFile.fileName());
                                } else if (embeddedMarker == QStringLiteral("cert")) {
                                    rv.insert(QStringLiteral("OpenVPN.Cert"), outputFile.fileName());
                                } else if (embeddedMarker == QStringLiteral("key")) {
                                    rv.insert(QStringLiteral("OpenVPN.Key"), outputFile.fileName());
                                } else {
                                    // Assume that the marker corresponds to the openvpn option, (such as 'tls-auth')
                                    extraOptions.append(embeddedMarker + QChar(' ') + outputFile.fileName());
                                }
                            }
                        }
                    }
                }
            }
            embeddedMarker.clear();
            embeddedContent.clear();
        } else if (!embeddedMarker.isEmpty()) {
            embeddedContent.append(line + QStringLiteral("\n"));
        } else {
            QStringList tokens(line.split(whitespace, QString::SkipEmptyParts));
            if (!tokens.isEmpty()) {
                // Find directives that become part of the connman configuration
                const QString& directive(tokens.front());
                const QStringList arguments(tokens.count() > 1 ? tokens.mid(1) : QStringList());

                if (directive == QStringLiteral("remote")) {
                    // Connman supports a single remote host - if we get further instances, pass them through the config file
                    if (!rv.contains(QStringLiteral("Host"))) {
                        if (arguments.count() > 0) {
                            rv.insert(QStringLiteral("Host"), arguments.at(0));
                        }
                        if (arguments.count() > 1) {
                            rv.insert(QStringLiteral("OpenVPN.Port"), arguments.at(1));
                        }
                        if (arguments.count() > 2) {
                            rv.insert(QStringLiteral("OpenVPN.Proto"), normaliseProtocol(arguments.at(2)));
                        }
                    } else {
                        extraOptions.append(line);
                    }
                } else if (directive == QStringLiteral("ca") ||
                           directive == QStringLiteral("cert") ||
                           directive == QStringLiteral("key") ||
                           directive == QStringLiteral("auth-user-pass")) {
                    if (!arguments.isEmpty()) {
                        // If these file paths are not absolute, assume they are in the same directory as the provisioning file
                        QString file(arguments.at(0));
                        if (!file.startsWith(QChar('/'))) {
                            const QFileInfo info(provisioningFile.fileName());
                            file = info.dir().absoluteFilePath(file);
                        }
                        if (directive == QStringLiteral("ca")) {
                            rv.insert(QStringLiteral("OpenVPN.CACert"), file);
                        } else if (directive == QStringLiteral("cert")) {
                            rv.insert(QStringLiteral("OpenVPN.Cert"), file);
                        } else if (directive == QStringLiteral("key")) {
                            rv.insert(QStringLiteral("OpenVPN.Key"), file);
                        } else if (directive == QStringLiteral("auth-user-pass")) {
                            rv.insert(QStringLiteral("OpenVPN.AuthUserPass"), file);
                        }
                    } else if (directive == QStringLiteral("auth-user-pass")) {
                        // Preserve this option to mean ask for credentials
                        rv.insert(QStringLiteral("OpenVPN.AuthUserPass"), QStringLiteral("-"));
                    }
                } else if (directive == QStringLiteral("mtu") ||
                           directive == QStringLiteral("tun-mtu")) {
                    // Connman appears to use a long obsolete form of this option...
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.MTU"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("ns-cert-type")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.NSCertType"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("proto")) {
                    if (!arguments.isEmpty()) {
                        // All values from a 'remote' directive to take precedence
                        if (!rv.contains(QStringLiteral("OpenVPN.Proto"))) {
                            rv.insert(QStringLiteral("OpenVPN.Proto"), normaliseProtocol(arguments.join(QChar(' '))));
                        }
                    }
                } else if (directive == QStringLiteral("port")) {
                    // All values from a 'remote' directive to take precedence
                    if (!rv.contains(QStringLiteral("OpenVPN.Port"))) {
                        if (!arguments.isEmpty()) {
                            rv.insert(QStringLiteral("OpenVPN.Port"), arguments.join(QChar(' ')));
                        }
                    }
                } else if (directive == QStringLiteral("askpass")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.AskPass"), arguments.join(QChar(' ')));
                    } else {
                        rv.insert(QStringLiteral("OpenVPN.AskPass"), QString());
                    }
                } else if (directive == QStringLiteral("auth-nocache")) {
                    rv.insert(QStringLiteral("OpenVPN.AuthNoCache"), QStringLiteral("true"));
                } else if (directive == QStringLiteral("tls-remote")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.TLSRemote"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("cipher")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.Cipher"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("auth")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.Auth"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("comp-lzo")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.CompLZO"), arguments.join(QChar(' ')));
                    } else {
                        rv.insert(QStringLiteral("OpenVPN.CompLZO"), QStringLiteral("adaptive"));
                    }
                } else if (directive == QStringLiteral("remote-cert-tls")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.RemoteCertTls"), arguments.join(QChar(' ')));
                    }
                } else {
                    // A directive that connman does not care about - pass through to the config file
                    extraOptions.append(line);
                }
            }
        }
    }

    if (!extraOptions.isEmpty()) {
        // Write a config file to contain the extra options
        QDir outputDir(provisioningOutputPath_);
        if (!outputDir.exists() && !outputDir.mkpath(provisioningOutputPath_)) {
            qCWarning(lcVpnLog) << "Unable to create base directory for VPN provisioning content:" << provisioningOutputPath_;
        } else {
            // Name the file according to content
            QCryptographicHash hash(QCryptographicHash::Sha1);
            foreach (const QString &line, extraOptions) {
                hash.addData(line.toUtf8());
            }

            const QString outputFileName(QString(hash.result().toHex()) + QStringLiteral(".conf"));
            QFile outputFile(outputDir.absoluteFilePath(outputFileName));
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qCWarning(lcVpnLog) << "Unable to write VPN provisioning configuration file:" << outputFile.fileName();
            } else {
                QTextStream os(&outputFile);
                foreach (const QString &line, extraOptions) {
                    os << line << endl;
                }

                rv.insert(QStringLiteral("OpenVPN.ConfigFile"), outputFile.fileName());
            }
        }
    }

    return rv;
}

bool VpnModel::domainInUse(const QString &domain) const
{
    if (defaultDomains_.contains(domain)) {
        return true;
    }

    const int itemCount(count());
    for (int index = 0; index < itemCount; ++index) {
        const VpnConnection *connection = get<VpnConnection>(index);
        if (connection->domain() == domain) {
            return true;
        }
    }
    return false;
}

QString VpnModel::createDefaultDomain() const
{
    QString newDomain = defaultDomain;
    int index = 1;
    while (domainInUse(newDomain)) {
        newDomain = defaultDomain + QString(".%1").arg(index);
        ++index;
    }
    return newDomain;
}

bool VpnModel::isDefaultDomain(const QString &domain) const
{
    if (domain == legacyDefaultDomain)
        return true;

    static const QRegularExpression domainPattern(QStringLiteral("^%1(\\.\\d+)?$").arg(defaultDomain));
    return domainPattern.match(domain).hasMatch();
}


VpnConnection::VpnConnection(const QString &path)
    : QObject(0)
    , path_(path)
    , state_(static_cast<int>(VpnModel::Disconnect))
    , type_("openvpn")
    , autoConnect_(false)
    , storeCredentials_(false)
    , immutable_(false)
    , index_(-1)
{
}

