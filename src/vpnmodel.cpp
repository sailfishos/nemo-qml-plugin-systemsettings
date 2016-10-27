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

#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDebug>


namespace {

// Conversion to/from DBus/QML
QHash<QString, QList<QPair<QVariant, QVariant> > > propertyConversions()
{
    QHash<QString, QList<QPair<QVariant, QVariant> > > rv;

    QList<QPair<QVariant, QVariant> > types;
    types.push_back(qMakePair(QVariant::fromValue(QString("openvpn")), QVariant::fromValue(static_cast<int>(VpnModel::OpenVPN))));
    types.push_back(qMakePair(QVariant::fromValue(QString("openconnect")), QVariant::fromValue(static_cast<int>(VpnModel::OpenConnect))));
    types.push_back(qMakePair(QVariant::fromValue(QString("vpnc")), QVariant::fromValue(static_cast<int>(VpnModel::VPNC))));
    types.push_back(qMakePair(QVariant::fromValue(QString("l2tp")), QVariant::fromValue(static_cast<int>(VpnModel::L2TP))));
    types.push_back(qMakePair(QVariant::fromValue(QString("pptp")), QVariant::fromValue(static_cast<int>(VpnModel::PPTP))));
    rv.insert(QString("type"), types);

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
            qWarning() << "No conversion found for" << (toDBus ? "QML" : "DBus") << "value:" << value << key;
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

}

VpnModel::VpnModel(QObject *parent)
    : ObjectListModel(parent, true, false)
    , connmanVpn_("net.connman.vpn", "/", QDBusConnection::systemBus(), this)
{
    qDBusRegisterMetaType<PathProperties>();
    qDBusRegisterMetaType<PathPropertiesArray>();

    connect(&connmanVpn_, &ConnmanVpnProxy::ConnectionAdded, [this](const QDBusObjectPath &objectPath, const QVariantMap &properties) {
        const QString path(objectPath.path());
        VpnConnection *conn = connection(path);
        if (!conn) {
            qWarning() << "Adding connection:" << path;
            conn = newConnection(path);
        }
        updateConnection(conn, propertiesToQml(properties));
    });

    connect(&connmanVpn_, &ConnmanVpnProxy::ConnectionRemoved, [this](const QDBusObjectPath &objectPath) {
        const QString path(objectPath.path());
        if (VpnConnection *conn = connection(path)) {
            qWarning() << "Removing obsolete connection:" << path;
            removeItem(conn);
            delete conn;
        } else {
            qWarning() << "Unable to remove unknown connection:" << path;
        }

        // Remove the proxy if present
        auto it = connections_.find(path);
        if (it != connections_.end()) {
            ConnmanVpnConnectionProxy *proxy(*it);
            connections_.erase(it);
            delete proxy;
        }
    });

    // If connman-vpn restarts, we need to discard and re-read the state
    QDBusServiceWatcher *watcher = new QDBusServiceWatcher("net.connman.vpn", QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &) {
        for (int i = 0, n = count(); i < n; ++i) {
            get(i)->deleteLater();
        }
        clear();
        setPopulated(false);
        qDeleteAll(connections_);
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

void VpnModel::createConnection(const QVariantMap &properties)
{
    const QString path(properties.value(QString("path")).toString());
    if (path.isEmpty()) {
        const QString host(properties.value(QString("host")).toString());
        const QString name(properties.value(QString("name")).toString());

        if (!host.isEmpty() && !name.isEmpty()) {
            QDBusPendingCall call = connmanVpn_.Create(propertiesToDBus(properties));

            QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
                QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                watcher->deleteLater();

                if (reply.isError()) {
                    qWarning() << "Unable to create Connman VPN connection:" << reply.error().message();
                } else {
                    const QDBusObjectPath &objectPath(reply.value());
                    qWarning() << "Created VPN connection:" << objectPath.path();
                }
            });
        } else {
            qWarning() << "Unable to create VPN connection without host and name properties";
        }
    } else {
        qWarning() << "Unable to create VPN connection with pre-existing path:" << path;
    }
}

void VpnModel::modifyConnection(const QString &path, const QVariantMap &properties)
{
    if (VpnConnection *conn = connection(path)) {
        // ConnmanVpnConnectionProxy provides the SetProperty interface to modify a connection,
        // but as far as I can tell, the only way to cause Connman to store the configuration to
        // disk is to create a new connection...  Work around this by removing the existing
        // connection and recreating it with the updated properties.
        qWarning() << "Removing VPN connection for modification:" << conn->path();
        deleteConnection(conn->path());

        QVariantMap updatedProperties(properties);
        updatedProperties.remove(QString("path"));
        updatedProperties.remove(QString("state"));
        updatedProperties.remove(QString("index"));
        updatedProperties.remove(QString("immutable"));

        QDBusPendingCall call = connmanVpn_.Create(propertiesToDBus(updatedProperties));

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, conn](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            watcher->deleteLater();

            if (reply.isError()) {
                qWarning() << "Unable to recreate Connman VPN connection:" << reply.error().message();
            } else {
                const QDBusObjectPath &objectPath(reply.value());
                qWarning() << "Modified VPN connection:" << objectPath.path();
            }
        });
    } else {
        qWarning() << "Unable to update unknown VPN connection:" << path;
    }
}

void VpnModel::deleteConnection(const QString &path)
{
    if (VpnConnection *conn = connection(path)) {
        Q_UNUSED(conn)

        QDBusPendingCall call = connmanVpn_.Remove(QDBusObjectPath(path));

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<void> reply = *watcher;
            watcher->deleteLater();

            if (reply.isError()) {
                qWarning() << "Unable to delete Connman VPN connection:" << path << ":" << reply.error().message();
            } else {
                qWarning() << "Deleted connection:" << path;
            }
        });
    } else {
        qWarning() << "Unable to delete unknown connection:" << path;
    }
}

void VpnModel::activateConnection(const QString &path)
{
    auto it = connections_.find(path);
    if (it != connections_.end()) {
        ConnmanVpnConnectionProxy *proxy(*it);

        QDBusPendingCall call = proxy->Connect();

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<void> reply = *watcher;
            watcher->deleteLater();

            if (reply.isError()) {
                qWarning() << "Unable to activate Connman VPN connection:" << path << ":" << reply.error().message();
            }
        });
    } else {
        qWarning() << "Unable to activate VPN connection without proxy:" << path;
    }
}

void VpnModel::deactivateConnection(const QString &path)
{
    auto it = connections_.find(path);
    if (it != connections_.end()) {
        ConnmanVpnConnectionProxy *proxy(*it);

        QDBusPendingCall call = proxy->Disconnect();

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            QDBusPendingReply<void> reply = *watcher;
            watcher->deleteLater();

            if (reply.isError()) {
                qWarning() << "Unable to deactivate Connman VPN connection:" << path << ":" << reply.error().message();
            }
        });
    } else {
        qWarning() << "Unable to deactivate VPN connection without proxy:" << path;
    }
}

QVariantMap VpnModel::connectionSettings(const QString &path)
{
    QVariantMap rv;
    if (VpnConnection *conn = connection(path)) {
        rv = itemRoles(conn);
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
            qWarning() << "Unable to fetch Connman VPN connections:" << reply.error().message();
        } else {
            const PathPropertiesArray &connections(reply.value());
            for (const PathProperties &connection : connections) {
                const QString &path(connection.first.path());
                const QVariantMap &properties(connection.second);

                VpnConnection *conn = newConnection(path);
                updateConnection(conn, propertiesToQml(properties));
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

    // Create a proxy for this connection
    ConnmanVpnConnectionProxy *proxy = new ConnmanVpnConnectionProxy("net.connman.vpn", path, QDBusConnection::systemBus(), nullptr);
    connections_.insert(path, proxy);

    connect(proxy, &ConnmanVpnConnectionProxy::PropertyChanged, this, [this, conn](const QString &name, const QDBusVariant &value) {
        QVariantMap properties;
        properties.insert(name, value.variant());
        updateConnection(conn, propertiesToQml(properties));
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

    if (updateItem(conn, properties)) {
        itemChanged(conn);

        const int itemCount(count());
        if (itemCount > 1) {
            // Keep the items sorted by name
            int index = 0;
            for ( ; index < itemCount; ++index) {
                const VpnConnection *existing = get<VpnConnection>(index);
                if (existing->name() > conn->name()) {
                    break;
                }
            }
            const int currentIndex = indexOf(conn);
            if (index != currentIndex && (index - 1) != currentIndex) {
                moveItem(currentIndex, (currentIndex < index ? (index - 1) : index));
            }
        }
    }
}


VpnConnection::VpnConnection(const QString &path)
    : QObject(0)
    , path_(path)
    , state_(static_cast<int>(VpnModel::Idle))
    , type_(static_cast<int>(VpnModel::OpenVPN))
{
}

