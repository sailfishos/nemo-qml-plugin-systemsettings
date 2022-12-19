/*
 * Copyright (c) 2016 - 2021 Jolla Ltd.
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

#include <QRegularExpression>
#include <QStandardPaths>
#include <QDataStream>
#include <QCryptographicHash>
#include <QQmlEngine>
#include <QDir>
#include <QXmlQuery>
#include <QXmlResultItems>
#include <QSettings>
#include "logging_p.h"
#include "vpnmanager.h"

#include "settingsvpnmodel.h"

namespace {

const auto defaultDomain = QStringLiteral("sailfishos.org");
const auto legacyDefaultDomain(QStringLiteral("merproject.org"));

int numericValue(VpnConnection::ConnectionState state)
{
    switch (state) {
    case VpnConnection::Ready:
        return 3;
    case VpnConnection::Configuration:
        return 2;
    case VpnConnection::Association:
        return 1;
    default:
        return 0;
    }
}

VpnConnection::ConnectionState getMaxState(VpnConnection::ConnectionState newState, VpnConnection::ConnectionState oldState)
{
    if (numericValue(newState) > numericValue(oldState)) {
        return newState;
    }

    return oldState;
}

} // end anonymous namespace

SettingsVpnModel::SettingsVpnModel(QObject* parent)
    : VpnModel(parent)
    , credentials_(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/system/privileged/vpn-data"))
    , bestState_(VpnConnection::Idle)
    , autoConnect_(false)
    , orderByConnected_(true)
    , provisioningOutputPath_(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/system/privileged/vpn-provisioning"))
    , roles(VpnModel::roleNames())
{
    VpnManager *manager = vpnManager();

    roles.insert(ConnectedRole, "connected");

    connect(manager, &VpnManager::connectionAdded, this, &SettingsVpnModel::connectionAdded, Qt::UniqueConnection);
    connect(manager, &VpnManager::connectionRemoved, this, &SettingsVpnModel::connectionRemoved, Qt::UniqueConnection);
    connect(manager, &VpnManager::connectionsRefreshed, this, &SettingsVpnModel::connectionsRefreshed, Qt::UniqueConnection);
}

SettingsVpnModel::~SettingsVpnModel()
{
    VpnManager *manager = vpnManager();

    disconnect(manager, 0, this, 0);
}

void SettingsVpnModel::createConnection(const QVariantMap &createProperties)
{
    QVariantMap properties(createProperties);
    const QString domain(properties.value(QString("domain")).toString());
    if (domain.isEmpty()) {
        properties.insert(QString("domain"), QVariant::fromValue(createDefaultDomain()));
    }

    vpnManager()->createConnection(properties);
}

QHash<int, QByteArray> SettingsVpnModel::roleNames() const
{
    return roles;
}

QVariant SettingsVpnModel::data(const QModelIndex &index, int role) const
{
    if (index.isValid() && index.row() >= 0 && index.row() < connections().count()) {
        switch (role) {
        case ConnectedRole:
            return QVariant::fromValue((bool)connections().at(index.row())->connected());
        default:
            return VpnModel::data(index, role);
        }
    }

    return QVariant();
}

VpnConnection::ConnectionState SettingsVpnModel::bestState() const
{
    return bestState_;
}

bool SettingsVpnModel::autoConnect() const
{
    return autoConnect_;
}

bool SettingsVpnModel::orderByConnected() const
{
    return orderByConnected_;
}

void SettingsVpnModel::setOrderByConnected(bool orderByConnected)
{
    if (orderByConnected != orderByConnected_) {
        orderByConnected_ = orderByConnected;
        VpnModel::connectionsChanged();
        emit orderByConnectedChanged();
    }
}

void SettingsVpnModel::modifyConnection(const QString &path, const QVariantMap &properties)
{
    VpnConnection *conn = vpnManager()->connection(path);
    if (conn) {
        QVariantMap updatedProperties(properties);
        const QString domain(updatedProperties.value(QString("domain")).toString());

        if (domain.isEmpty()) {
            if (isDefaultDomain(conn->domain())) {
                // The connection already has a default domain, no need to change it
                updatedProperties.remove("domain");
            }
            else {
                updatedProperties.insert(QString("domain"), QVariant::fromValue(createDefaultDomain()));
            }
        }

        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool couldStoreCredentials(credentials_.credentialsExist(location));
        const bool canStoreCredentials(properties.value(QString("storeCredentials")).toBool());

        vpnManager()->modifyConnection(path, updatedProperties);

        if (canStoreCredentials != couldStoreCredentials) {
            if (canStoreCredentials) {
                credentials_.storeCredentials(location, QVariantMap());
            } else {
                credentials_.removeCredentials(location);
            }
        }
    }
    else {
        qCWarning(lcVpnLog) << "VPN connection modification failed: connection doesn't exist";
    }
}

void SettingsVpnModel::deleteConnection(const QString &path)
{
    if (VpnConnection *conn = vpnManager()->connection(path)) {
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
                    for (VpnConnection *c : connections()) {
                        if (filename == c->providerProperties().value(property).toString()) {
                            timesUsed++;
                            if (timesUsed > 1) {
                                break;
                            }
                        }
                    }

                    if (timesUsed > 1) {
                        qCInfo(lcVpnLog) << "VPN provisioning file kept, used by" << timesUsed << "connections.";
                        continue;
                    }

                    qCInfo(lcVpnLog) << "VPN provisioning file removed: " << filename;
                    if (!QFile::remove(filename)) {
                        qCWarning(lcVpnLog) << "VPN provisioning file could not be removed: " << filename;
                    }
                }
            }
        }

        vpnManager()->deleteConnection(path);
    }

}

void SettingsVpnModel::activateConnection(const QString &path)
{
    vpnManager()->activateConnection(path);
}

void SettingsVpnModel::deactivateConnection(const QString &path)
{
    vpnManager()->deactivateConnection(path);
}

VpnConnection *SettingsVpnModel::get(int index) const
{
    if (index >= 0 && index < connections().size()) {
        VpnConnection *item(connections().at(index));
        QQmlEngine::setObjectOwnership(item, QQmlEngine::CppOwnership);
        return item;
    }

    return 0;

}

// ==========================================================================
// QAbstractListModel Ordering
// ==========================================================================

bool SettingsVpnModel::compareConnections(const VpnConnection *i, const VpnConnection *j)
{
    return ((orderByConnected_ && (i->connected() > j->connected()))
            || ((!orderByConnected_ || (i->connected() == j->connected()))
                && (i->name().localeAwareCompare(j->name()) <= 0)));
}

void SettingsVpnModel::orderConnections(QVector<VpnConnection*> &connections)
{
    std::sort(connections.begin(), connections.end(), [this](const VpnConnection *i, const VpnConnection *j) -> bool {
        // Return true if i should appear before j in the list
        return compareConnections(i, j);
    });
}

void SettingsVpnModel::reorderConnection(VpnConnection * conn)
{
    const int itemCount(connections().size());

    if (itemCount > 1) {
        int index = 0;
        for ( ; index < itemCount; ++index) {
            const VpnConnection *existing = connections().at(index);
            // Scenario 1 orderByConnected == true: order first by connected, second by name
            // Scenario 2 orderByConnected == false: order only by name
            if (!compareConnections(existing, conn)) {
                break;
            }
        }
        const int currentIndex = connections().indexOf(conn);
        if (index != currentIndex && (index - 1) != currentIndex) {
            moveItem(currentIndex, (currentIndex < index ? (index - 1) : index));
        }
    }
}

void SettingsVpnModel::updatedConnectionPosition()
{
    VpnConnection *conn = qobject_cast<VpnConnection *>(sender());
    reorderConnection(conn);
}

void SettingsVpnModel::connectedChanged()
{
    VpnConnection *conn = qobject_cast<VpnConnection *>(sender());

    int row = connections().indexOf(conn);
    if (row >= 0) {
        QModelIndex index = createIndex(row, 0);;
        emit dataChanged(index, index);
    }
    reorderConnection(conn);
}

void SettingsVpnModel::connectionAdded(const QString &path)
{
    qCDebug(lcVpnLog) << "VPN connection added";
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        bool credentialsExist = credentials_.credentialsExist(CredentialsRepository::locationForObjectPath(path));
        conn->setStoreCredentials(credentialsExist);

        connect(conn, &VpnConnection::nameChanged, this, &SettingsVpnModel::updatedConnectionPosition, Qt::UniqueConnection);
        connect(conn, &VpnConnection::connectedChanged, this, &SettingsVpnModel::connectedChanged, Qt::UniqueConnection);
        connect(conn, &VpnConnection::stateChanged, this, &SettingsVpnModel::stateChanged, Qt::UniqueConnection);
    }
}

void SettingsVpnModel::connectionRemoved(const QString &path)
{
    qCDebug(lcVpnLog) << "VPN connection removed";
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        disconnect(conn, 0, this, 0);
    }
}

void SettingsVpnModel::connectionsRefreshed()
{
    qCDebug(lcVpnLog) << "VPN connections refreshed";
    QVector<VpnConnection*> connections = vpnManager()->connections();

    // Check to see if the best state has changed
    VpnConnection::ConnectionState maxState = VpnConnection::Idle;
    for (VpnConnection *conn : connections) {
        connect(conn, &VpnConnection::nameChanged, this, &SettingsVpnModel::updatedConnectionPosition, Qt::UniqueConnection);
        connect(conn, &VpnConnection::connectedChanged, this, &SettingsVpnModel::connectedChanged, Qt::UniqueConnection);
        connect(conn, &VpnConnection::stateChanged, this, &SettingsVpnModel::stateChanged, Qt::UniqueConnection);

        maxState = getMaxState(conn->state(), maxState);
    }

    updateBestState(maxState);
}

void SettingsVpnModel::stateChanged()
{
    // Emit the state changed signal needed for the VPN EnableSwitch
    VpnConnection *conn = qobject_cast<VpnConnection *>(sender());
    emit connectionStateChanged(conn->path(), conn->state());

    // Check to see if the best state has changed
    VpnConnection::ConnectionState maxState = getMaxState(conn->state(), VpnConnection::Idle);
    updateBestState(maxState);
}

// ==========================================================================
// Automatic domain allocation
// ==========================================================================

bool SettingsVpnModel::domainInUse(const QString &domain) const
{
    const int itemCount(count());
    for (int index = 0; index < itemCount; ++index) {
        const VpnConnection *connection = connections().at(index);
        if (connection->domain() == domain) {
            return true;
        }
    }
    return false;
}

QString SettingsVpnModel::createDefaultDomain() const
{
    QString newDomain = defaultDomain;
    int index = 1;
    while (domainInUse(newDomain)) {
        newDomain = defaultDomain + QString(".%1").arg(index);
        ++index;
    }
    return newDomain;
}

bool SettingsVpnModel::isDefaultDomain(const QString &domain)
{
    if (domain == legacyDefaultDomain)
        return true;

    static const QRegularExpression domainPattern(QStringLiteral("^%1(\\.\\d+)?$").arg(defaultDomain));
    return domainPattern.match(domain).hasMatch();
}

// ==========================================================================
// Credential storage
// ==========================================================================

QVariantMap SettingsVpnModel::connectionCredentials(const QString &path)
{
    QVariantMap rv;

    if (VpnConnection *conn = vpnManager()->connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool enabled(credentials_.credentialsExist(location));

        if (enabled) {
            rv = credentials_.credentials(location);
        } else {
            qWarning() << "VPN does not permit credentials storage:" << path;
        }

        conn->setStoreCredentials(enabled);
    } else {
        qWarning() << "Unable to return credentials for unknown VPN connection:" << path;
    }

    return rv;
}

void SettingsVpnModel::setConnectionCredentials(const QString &path, const QVariantMap &credentials)
{
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        credentials_.storeCredentials(CredentialsRepository::locationForObjectPath(path), credentials);

        conn->setStoreCredentials(true);
    } else {
        qWarning() << "Unable to set credentials for unknown VPN connection:" << path;
    }
}

bool SettingsVpnModel::connectionCredentialsEnabled(const QString &path)
{
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        const bool enabled(credentials_.credentialsExist(location));

        conn->setStoreCredentials(enabled);
        return enabled;
    } else {
        qWarning() << "Unable to test credentials storage for unknown VPN connection:" << path;
    }

    return false;
}

void SettingsVpnModel::disableConnectionCredentials(const QString &path)
{
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        const QString location(CredentialsRepository::locationForObjectPath(path));
        if (credentials_.credentialsExist(location)) {
            credentials_.removeCredentials(location);
        }

        conn->setStoreCredentials(false);
    } else {
        qWarning() << "Unable to set automatic connection for unknown VPN connection:" << path;
    }
}

QVariantMap SettingsVpnModel::connectionSettings(const QString &path)
{
    QVariantMap properties;
    if (VpnConnection *conn = vpnManager()->connection(path)) {
        // Check if the credentials storage has been changed
        const QString location(CredentialsRepository::locationForObjectPath(path));
        conn->setStoreCredentials(credentials_.credentialsExist(location));

        properties = VpnModel::connectionSettings(path);
    }
    return properties;
}

// ==========================================================================
// CredentialsRepository
// ==========================================================================

SettingsVpnModel::CredentialsRepository::CredentialsRepository(const QString &path)
    : baseDir_(path)
{
    if (!baseDir_.exists() && !baseDir_.mkpath(path)) {
        qWarning() << "Unable to create base directory for VPN credentials:" << path;
    }
}

QString SettingsVpnModel::CredentialsRepository::locationForObjectPath(const QString &path)
{
    int index = path.lastIndexOf(QChar('/'));
    if (index != -1) {
        return path.mid(index + 1);
    }

    return QString();
}

bool SettingsVpnModel::CredentialsRepository::credentialsExist(const QString &location) const
{
    // Test the FS, as another process may store/remove the credentials
    return baseDir_.exists(location);
}

bool SettingsVpnModel::CredentialsRepository::storeCredentials(const QString &location, const QVariantMap &credentials)
{
    QFile credentialsFile(baseDir_.absoluteFilePath(location));
    if (!credentialsFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Unable to write credentials file:" << credentialsFile.fileName();
        return false;
    } else {
        credentialsFile.write(encodeCredentials(credentials));
        credentialsFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther | QFileDevice::WriteOther);
        credentialsFile.close();
    }

    return true;
}

bool SettingsVpnModel::CredentialsRepository::removeCredentials(const QString &location)
{
    if (baseDir_.exists(location)) {
        if (!baseDir_.remove(location)) {
            qWarning() << "Unable to delete credentials file:" << location;
            return false;
        }
    }

    return true;
}

QVariantMap SettingsVpnModel::CredentialsRepository::credentials(const QString &location) const
{
    QVariantMap rv;

    QFile credentialsFile(baseDir_.absoluteFilePath(location));
    if (!credentialsFile.open(QIODevice::ReadOnly)) {
        qWarning() << "Unable to read credentials file:" << credentialsFile.fileName();
    } else {
        const QByteArray encoded = credentialsFile.readAll();
        credentialsFile.close();

        rv = decodeCredentials(encoded);
    }

    return rv;
}

QByteArray SettingsVpnModel::CredentialsRepository::encodeCredentials(const QVariantMap &credentials)
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

QVariantMap SettingsVpnModel::CredentialsRepository::decodeCredentials(const QByteArray &encoded)
{
    QVariantMap rv;

    QByteArray decoded(QByteArray::fromBase64(encoded));

    QDataStream is(decoded);
    is.setVersion(QDataStream::Qt_5_6);

    quint32 version;
    is >> version;

    if (version != 1u) {
        qWarning() << "Invalid version for stored credentials:" << version;
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

// ==========================================================================
// Provisioning files
// ==========================================================================

QVariantMap SettingsVpnModel::processProvisioningFile(const QString &path, const QString &type)
{
    QVariantMap rv;

    QFile provisioningFile(path);
    if (provisioningFile.open(QIODevice::ReadOnly)) {
        if (type == QStringLiteral("openvpn")) {
            rv = processOpenVpnProvisioningFile(provisioningFile);
        } else if (type == QStringLiteral("openconnect")) {
            rv = processOpenconnectProvisioningFile(provisioningFile);
        } else if (type == QStringLiteral("openfortivpn")) {
            rv = processOpenfortivpnProvisioningFile(provisioningFile);
        } else if (type == QStringLiteral("vpnc")) {
            rv = processVpncProvisioningFile(provisioningFile);
        } else if (type == QStringLiteral("l2tp")) {
            if (path.endsWith(QStringLiteral(".pbk"))) {
                rv = processPbkProvisioningFile(provisioningFile, type);
            } else {
                rv = processL2tpProvisioningFile(provisioningFile);
            }
        } else if (type == QStringLiteral("pptp")) {
            rv = processPbkProvisioningFile(provisioningFile, type);
        } else {
            qWarning() << "Provisioning not currently supported for VPN type:" << type;
        }
    } else {
        qWarning() << "Unable to open provisioning file:" << path;
    }

    return rv;
}

QVariantMap SettingsVpnModel::processOpenVpnProvisioningFile(QFile &provisioningFile)
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
                qWarning() << "Invalid embedded content";
            }
        } else if (line.contains(embeddedTrailer, &match)) {
            const QString marker = match.captured(1);
            if (marker != embeddedMarker) {
                qWarning() << "Invalid embedded content:" << marker << "!=" << embeddedMarker;
            } else {
                if (embeddedContent.isEmpty()) {
                    qWarning() << "Ignoring empty embedded content:" << embeddedMarker;
                } else {
                    if (embeddedMarker == QStringLiteral("connection")) {
                        // Special case: not embedded content, but a <connection> structure - pass through as an extra option
                        extraOptions.append(QStringLiteral("<connection>\n") + embeddedContent + QStringLiteral("</connection>"));
                    } else {
                        // Embedded content
                        QDir outputDir(provisioningOutputPath_);
                        if (!outputDir.exists() && !outputDir.mkpath(provisioningOutputPath_)) {
                            qWarning() << "Unable to create base directory for VPN provisioning content:" << provisioningOutputPath_;
                        } else {
                            // Name the file according to content
                            QCryptographicHash hash(QCryptographicHash::Sha1);
                            hash.addData(embeddedContent.toUtf8());

                            const QString outputFileName(QString(hash.result().toHex()) + QChar('.') + embeddedMarker);
                            QFile outputFile(outputDir.absoluteFilePath(outputFileName));
                            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                qWarning() << "Unable to write VPN provisioning content file:" << outputFile.fileName();
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
                } else if (directive == QStringLiteral("ping")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.Ping"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("ping-exit")) {
                    if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.PingExit"), arguments.join(QChar(' ')));
                    }
                } else if (directive == QStringLiteral("remap-usr1")) {
                     if (!arguments.isEmpty()) {
                        rv.insert(QStringLiteral("OpenVPN.RemapUsr1"), arguments.join(QChar(' ')));
                     }
                } else if (directive == QStringLiteral("ping-restart")) {
                    // Ignore, must not be set with ConnMan
                    qInfo() << "Ignoring ping-restart with OpenVPN";
                } else if (directive == QStringLiteral("connect-retry-max")) {
                    // Ignore, must not be set with ConnMan
                    qInfo() << "Ignoring connect-retry-max with OpenVPN";
                } else if (directive == QStringLiteral("block-ipv6")) {
                     if (!arguments.isEmpty()) {
                         rv.insert(QStringLiteral("OpenVPN.BlockIPv6"), arguments.join(QChar(' ')));
                     }
                } else if (directive == QStringLiteral("up") || directive == QStringLiteral("down")) {
                    // Ignore both up and down scripts as they would interfere with ConnMan and
                    // we do not ship any OpenVPN scripts with the package.
                    qInfo() << "Ignoring " << directive << " script";
                } else {
                    // A directive that ConnMan does not care about - pass through to the config file
                    extraOptions.append(line);
                }
            }
        }
    }

    if (!extraOptions.isEmpty()) {
        // Write a config file to contain the extra options
        QDir outputDir(provisioningOutputPath_);
        if (!outputDir.exists() && !outputDir.mkpath(provisioningOutputPath_)) {
            qWarning() << "Unable to create base directory for VPN provisioning content:" << provisioningOutputPath_;
        } else {
            // Name the file according to content
            QCryptographicHash hash(QCryptographicHash::Sha1);
            foreach (const QString &line, extraOptions) {
                hash.addData(line.toUtf8());
            }

            const QString outputFileName(QString(hash.result().toHex()) + QStringLiteral(".conf"));
            QFile outputFile(outputDir.absoluteFilePath(outputFileName));
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qWarning() << "Unable to write VPN provisioning configuration file:" << outputFile.fileName();
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

QVariantMap SettingsVpnModel::processVpncProvisioningFile(QFile &provisioningFile)
{
    QVariantMap rv;

    QTextStream is(&provisioningFile);
#define ENTRY(x, y, z) { QStringLiteral(x), QStringLiteral(y), z }
    static const struct {
        QString key;
        QString targetProperty;
        bool hasValue;
    } options[] = {
        ENTRY("IPSec gateway", "Host", true),
        ENTRY("IPSec ID", "VPNC.IPSec.ID", true),
        ENTRY("Domain", "VPNC.Domain", true),
        ENTRY("Vendor", "VPNC.Vendor", true),
        ENTRY("IKE DH Group", "VPNC.IKE.DHGroup", true),
        ENTRY("Perfect Forward Secrecy", "VPNC.PFS", true),
        ENTRY("NAT Traversal Mode", "VPNC.NATTMode", true),
        ENTRY("Enable Single DES", "VPNC.SingleDES", false),
        ENTRY("Enable no encryption", "VPNC.NoEncryption", false),
        ENTRY("Application version", "VPNC.AppVersion", true),
        ENTRY("Local Port", "VPNC.LocalPort", true),
        ENTRY("Cisco UDP Encapsulation Port", "VPNC.CiscoPort", true),
        ENTRY("DPD idle timeout (our side)", "VPNC.DPDTimeout", true),
        ENTRY("IKE Authmode", "VPNC.IKE.AuthMode", true),
        /* Unhandled config options
        ENTRY("IPSec secret", "VPNC.IPSec.Secret", true),
        ENTRY("IPSec obfuscated secret", "?", true),
        ENTRY("Xauth username", "VPNC.XAuth.Username", true),
        ENTRY("Xauth password", "VPNC.XAuth.Password", true),
        ENTRY("Xauth obfuscated password", "?", true),
        ENTRY("Xauth interactive", "?", false),
        ENTRY("Script", "?", true),
        ENTRY("Interface name", "?", true),
        ENTRY("Interface mode", "?", true),
        ENTRY("Interface MTU", "?", true),
        ENTRY("Debug", "?", true),
        ENTRY("No Detach", "?", false),
        ENTRY("Pidfile", "?", true),
        ENTRY("Local Addr", "?", true),
        ENTRY("Noninteractive", "?", false),
        ENTRY("CA-File", "?", true),
        ENTRY("CA-Dir", "?", true),
        ENTRY("IPSEC target network", "?", true),
        ENTRY("Password helper", "?", true),
        */
    };
#undef ENTRY
    while (!is.atEnd()) {
        QString line(is.readLine());

        for (size_t i = 0; i < sizeof(options) / sizeof(*options); i++) {
            if (!line.startsWith(options[i].key, Qt::CaseInsensitive)) {
                continue;
            }
            if (!options[i].hasValue) {
                rv[options[i].targetProperty] = true;
            } else {
                int pos = options[i].key.length();
                if (line.length() == pos
                    || (line[pos] != ' '
                        && line[pos] != '\t')) {
                    continue;
                }
                rv[options[i].targetProperty] = line.mid(pos + 1);
            }
        }
    }

    if (rv.contains("VPNC.IPSec.ID")) {
        if (rv.contains("Host")) {
            rv["Name"] = QStringLiteral("%1 %2").arg(rv["Host"].value<QString>()).arg(rv["VPNC.IPSec.ID"].value<QString>());
        } else {
            rv["Name"] = rv["VPNC.IPSec.ID"];
        }
    } else {
        rv["Name"] = QFileInfo(provisioningFile).baseName();
    }

    return rv;
}

QVariantMap SettingsVpnModel::processOpenconnectProvisioningFile(QFile &provisioningFile)
{
    char first;
    QVariantMap rv;

    if (provisioningFile.peek(&first, 1) != 1) {
        return QVariantMap();
    }

    if (first == '<') {
#define NS "declare default element namespace \"http://schemas.xmlsoap.org/encoding/\"; "
        QXmlQuery query;
        QXmlResultItems entries;

        if (!query.setFocus(&provisioningFile)) {
            qWarning() << "Unable to read provisioning configuration file";
            return QVariantMap();
        }

        query.setQuery(QStringLiteral(NS "/AnyConnectProfile/ServerList/HostEntry"));
        query.evaluateTo(&entries);
        if (!query.isValid()) {
            qWarning() << "Unable to query provisioning configuration file";
            return QVariantMap();
        }

        for (QXmlItem entry = entries.next(); !entry.isNull(); entry = entries.next()) {
            QXmlQuery subQuery(query.namePool());
            QStringList name;
            QStringList address;
            QStringList userGroup;
            subQuery.setFocus(entry);
            subQuery.setQuery(QStringLiteral(NS "normalize-space(HostName[1]/text())"));
            subQuery.evaluateTo(&name);
            subQuery.setQuery(QStringLiteral(NS "normalize-space(HostAddress[1]/text())"));
            subQuery.evaluateTo(&address);
            subQuery.setQuery(QStringLiteral(NS "normalize-space(UserGroup[1]/text())"));
            subQuery.evaluateTo(&userGroup);

            if (!name[0].isEmpty()) {
                rv.insert(QStringLiteral("Name"), name[0]);
            }

            if (!address[0].isEmpty()) {
                rv.insert(QStringLiteral("Host"), address[0]);
            }

            if (!userGroup[0].isEmpty()) {
                rv.insert(QStringLiteral("OpenConnect.Usergroup"), userGroup[0]);
            }
        }
    } else {
        struct ArgMapping {
            bool hasArgument;
            QString targetProperty;
        };
#define ENTRY(x, y, z) { QStringLiteral(x), { y, QStringLiteral(z) }}
        static const QHash<QString, ArgMapping> fields {
            ENTRY("user", true, "OpenConnect.Username"),
            ENTRY("certificate", true, "OpenConnect.ClientCert"),
            ENTRY("sslkey", true, "OpenConnect.UserPrivateKey"),
            ENTRY("key-password", true, "OpenConnect.PKCSPassword"),
            ENTRY("cookie", true, "OpenConnect.Cookie"),
            ENTRY("cafile", true, "OpenConnect.CACert"),
            ENTRY("disable-ipv6", false, "OpenConnect.DisableIPv6"),
            ENTRY("protocol", true, "OpenConnect.Protocol"),
            ENTRY("no-http-keepalive", false, "OpenConnect.NoHTTPKeepalive"),
            ENTRY("servercert", true, "OpenConnect.ServerCert"),
            ENTRY("usergroup", true, "OpenConnect.Usergroup"),
            ENTRY("base-mtu", true, "OpenConnect.MTU"),
        };
#undef ENTRY
        QTextStream is(&provisioningFile);

        const QRegularExpression commentLine(QStringLiteral("^\\s*(?:\\#|$)"));
        const QRegularExpression record(QStringLiteral("^\\s*([^ \\t=]+)\\s*(?:=\\s*|)(.*?)$"));

        while (!is.atEnd()) {
            QString line(is.readLine());

            if (line.contains(commentLine)) {
                continue;
            }

            QRegularExpressionMatch match = record.match(line);

            if (!match.hasMatch()) {
                continue;
            }

            QString field = match.captured(1);
            auto i = fields.find(field);

            if (i != fields.end()) {
                if (i.value().hasArgument) {
                    if (!match.captured(2).isEmpty()) {
                        rv[i.value().targetProperty] = match.captured(2);
                    }
                } else {
                    rv[i.value().targetProperty] = true;
                }
            }
        }

        if (rv.contains(QStringLiteral("OpenConnect.UserPrivateKey"))) {
            rv[QStringLiteral("OpenConnect.AuthType")] = QStringLiteral("publickey");
        } else if (rv.contains(QStringLiteral("OpenConnect.ClientCert"))) {
            rv[QStringLiteral("OpenConnect.PKCSClientCert")] = rv[QStringLiteral("OpenConnect.ClientCert")];
            rv.remove(QStringLiteral("OpenConnect.ClientCert"));
            rv[QStringLiteral("OpenConnect.AuthType")] = QStringLiteral("pkcs");
        } else if (rv.contains(QStringLiteral("OpenConnect.Username"))) {
            if (rv.contains(QStringLiteral("OpenConnect.Cookie"))) {
                rv[QStringLiteral("OpenConnect.AuthType")] = QStringLiteral("cookie_with_userpass");
            } else {
                rv[QStringLiteral("OpenConnect.AuthType")] = QStringLiteral("userpass");
            }
        } else if (rv.contains(QStringLiteral("OpenConnect.Cookie"))) {
            rv[QStringLiteral("OpenConnect.AuthType")] = QStringLiteral("cookie");
        }

        if (!rv.isEmpty()) {
            // The config file does not have server name, guess file name instead
            QString fileName = provisioningFile.fileName();
            int slashPos = fileName.lastIndexOf('/');
            int dotPos = fileName.lastIndexOf('.');
            rv[QStringLiteral("Host")] = fileName.mid(slashPos + 1, dotPos - slashPos - 1);
        }
    }

    return rv;
#undef NS
}

QVariantMap SettingsVpnModel::processOpenfortivpnProvisioningFile(QFile &provisioningFile)
{
    char first;
    QVariantMap rv;
    QStringList option;

    if (provisioningFile.peek(&first, 1) != 1) {
        return QVariantMap();
    }

    if (first == '<') {
        QXmlQuery query;
        QXmlResultItems entries;

        if (!query.setFocus(&provisioningFile)) {
            qWarning() << "Unable to read provisioning configuration file";
            return QVariantMap();
        }

        query.setQuery(QStringLiteral("/forticlient_configuration/vpn/sslvpn/connections/connection"));
        query.evaluateTo(&entries);
        if (!query.isValid()) {
            qWarning() << "Unable to query provisioning configuration file";
            return QVariantMap();
        }

        for (QXmlItem entry = entries.next(); !entry.isNull(); entry = entries.next()) {
            QXmlQuery subQuery(query.namePool());
            QStringList name;
            QStringList address;
            QStringList userGroup;
            subQuery.setFocus(entry);

            // Other fields that might be of interest
            // username
            // password
            // warn_invalid_server_certificate
            subQuery.setQuery(QStringLiteral("normalize-space(name[1]/text())"));
            subQuery.evaluateTo(&name);
            subQuery.setQuery(QStringLiteral("normalize-space(server[1]/text())"));
            subQuery.evaluateTo(&address);

            if (!name[0].isEmpty()) {
                rv.insert(QStringLiteral("Name"), name[0]);
            }

            if (!address[0].isEmpty()) {
                int pos = address[0].indexOf(':');
                if (pos == -1) {
                    rv.insert(QStringLiteral("Host"), address[0]);
                } else {
                    rv.insert(QStringLiteral("Host"), address[0].left(pos));
                    rv.insert(QStringLiteral("openfortivpn.Port"), address[0].midRef(pos + 1).toInt());
                }

                // We have a connection address, ignore the rest.
                break;
            }
        }

        // There's also other boolean (1/0) options under sslvpn/options:
        // preferred_dtls_tunnel
        // no_dhcp_server_route
        // keep_connection_alive
        query.setQuery(QStringLiteral("normalize-space(/forticlient_configuration/vpn/sslvpn/options/disallow_invalid_server_certificate/text())"));
        query.evaluateTo(&option);
        if (option[0] == QLatin1String("0")) {
            rv.insert(QStringLiteral("openfortivpn.AllowSelfSignedCert"), QStringLiteral("true"));
        }

    } else {
        QTextStream is(&provisioningFile);

        const QRegularExpression commentLine(QStringLiteral("^\\#"));
        const QRegularExpression record(QStringLiteral("^\\s*([^=]+)\\s*=\\s*(.*?)\\s*$"));
#define ENTRY(x, y) { QStringLiteral(x), QStringLiteral(y) }
        const QHash<QString, QString> fields {
            ENTRY("host", "Host"),
            ENTRY("port", "openfortivpn.Port"),
            ENTRY("trusted-cert", "openfortivpn.TrustedCert"),
            // possibly useful fields for the future, not supported by connman plugin
            // ENTRY("username", "?"),
            // ENTRY("password", "?"),
            // ENTRY("no-ftm-push", "?"),
            // ENTRY("realm", "?"),
            // ENTRY("ca-file", "?"),
            // ENTRY("user-cert", "?"),
            // ENTRY("user-key", "?"),
            // ENTRY("insercure-ssl", "?"),
            // ENTRY("cipher-list", "?"),
            // ENTRY("user-agent", "?"),
            // ENTRY("hostcheck", "?"),
        };
#undef ENTRY

        while (!is.atEnd()) {
            QString line(is.readLine());

            if (line.contains(commentLine)) {
                continue;
            }

            QRegularExpressionMatch match = record.match(line);

            if (!match.hasMatch()) {
                continue;
            }

            QString field = match.captured(1);
            auto i = fields.find(field);

            if (i != fields.end()) {
                rv[i.value()] = match.captured(2);
            }
        }
    }

    return rv;
}

bool SettingsVpnModel::processPppdProvisioningFile(QFile &provisioningFile, QVariantMap &result)
{
#define ENTRY(x, y) { QStringLiteral(x), QStringLiteral(y) }
    const QHash<QString, QString> stringOpts {
        ENTRY("lcp-echo-failure", "PPPD.EchoFailure"),
        ENTRY("lcp-echo-interval", "PPPD.EchoInterval"),
    };

    const QHash<QString, QString> boolOpts {
        ENTRY("debug", "PPPD.Debug"),
        ENTRY("refuse-eap", "PPPD.RefuseEAP"),
        ENTRY("refuse-pap", "PPPD.RefusePAP"),
        ENTRY("refuse-chap", "PPPD.RefuseCHAP"),
        ENTRY("refuse-mschap", "PPPD.RefuseMSCHAP"),
        ENTRY("refuse-mschapv2", "PPPD.RefuseMSCHAP2"),
        ENTRY("nobsdcomp", "PPPD.NoBSDComp"),
        ENTRY("nopcomp", "PPPD.NoPcomp"),
        ENTRY("noaccomp", "PPPD.UseAccomp"),
        ENTRY("nodeflate", "PPPD.NoDeflate"),
        ENTRY("require-mppe", "PPPD.ReqMPPE"),
        ENTRY("require-mppe-40", "PPPD.ReqMPPE40"),
        ENTRY("require-mppe-128", "PPPD.ReqMPPE128"),
        ENTRY("mppe-stateful", "PPPD.ReqMPPEStateful"),
        ENTRY("novj", "PPPD.NoVJ"),
        ENTRY("noipv6", "PPPD.NoIPv6"),
    };
#undef ENTRY

    QTextStream is(&provisioningFile);
    const QRegularExpression nonCommentRe(R"!((?:[^#]|[^\]|"[^"*]"|'[^']*')*)!");
    const QRegularExpression keyValueRe(R"!(^([^\s]*)\s+(?:"([^"]*)"|'([^']*)'|([^\s"']*))$)!");

    while (!is.atEnd()) {
        QString line(is.readLine());
        QString trimmed = nonCommentRe.match(line).captured(0).trimmed();

        if (trimmed.isEmpty()) {
            continue;
        }

        if (boolOpts.contains(trimmed)) {
            result[boolOpts[trimmed]] = true;
        } else {
            QRegularExpressionMatch match = keyValueRe.match(trimmed);

            if (match.isValid()) {
                QString key = match.captured(1);

                if (stringOpts.contains(key)) {
                    result[stringOpts[key]] = match.captured(2) + match.captured(3) + match.captured(4);
                }
            }
        }
    }

    return true;
}

QVariantMap SettingsVpnModel::processL2tpProvisioningFile(QFile &provisioningFile)
{
    QString provisioningFileName = provisioningFile.fileName();
    QSettings settings(provisioningFileName, QSettings::IniFormat);
    QStringList groups = settings.childGroups();
    QVariantMap rv;

#define ENTRY(x, y) { QStringLiteral(x), QStringLiteral(y) }
    const QHash<QString, QString> globalOptions {
        ENTRY("access control", "L2TP.AccessControl"),
        ENTRY("auth file", "L2TP.AuthFile"),
        ENTRY("force userspace", "L2TP.ForceUserSpace"),
        ENTRY("listen-addr", "L2TP.ListenAddr"),
        ENTRY("rand source", "L2TP.Rand Source"),
        ENTRY("ipsec saref", "L2TP.IPsecSaref"),
        ENTRY("port", "L2TP.Port"),
    };

    const QHash<QString, QString> lacOptions {
        ENTRY("lns", "Host"),
        ENTRY("bps", "L2TP.BPS"),
        ENTRY("tx bps", "L2TP.TXBPS"),
        ENTRY("rx bps", "L2TP.RXBPS"),
        ENTRY("length bit", "L2TP.LengthBit"),
        ENTRY("challenge", "L2TP.Challenge"),
        ENTRY("defaultroute", "L2TP.DefaultRoute"),
        ENTRY("flow bit", "L2TP.FlowBit"),
        ENTRY("tunnel rws", "L2TP.TunnelRWS"),
        ENTRY("autodial", "L2TP.Autodial"),
        ENTRY("redial", "L2TP.Redial"),
        ENTRY("redial timeout", "L2TP.RedialTimeout"),
        ENTRY("max redials", "L2TP.MaxRedials"),
        ENTRY("require pap", "L2TP.RequirePAP"),
        ENTRY("require chap", "L2TP.RequireCHAP"),
        ENTRY("require authentication", "L2TP.ReqAuth"),
        ENTRY("pppoptfile", "__PPP_FILE"),
    };
#undef ENTRY

    settings.beginGroup(QStringLiteral("global"));
    for (const auto &key : settings.allKeys()) {
        if (globalOptions.contains(key)) {
            rv[globalOptions[key]] = settings.value(key);
        }
    }
    settings.endGroup();

    if (groups.contains(QStringLiteral("lac default"))) {
        settings.beginGroup(QStringLiteral("lac default"));
        for (const auto &key : settings.allKeys()) {
            if (lacOptions.contains(key)) {
                rv[lacOptions[key]] = settings.value(key);
            }
        }
        settings.endGroup();
    }

    for (const auto &group : groups) {
        if (group.startsWith(QLatin1String("lac ")) && group != QLatin1String("lac default")) {
            rv[QStringLiteral("Name")] = group.mid(4);
            settings.beginGroup(group);
            for (const auto &key : settings.allKeys()) {
                if (lacOptions.contains(key)) {
                    rv[lacOptions[key]] = settings.value(key);
                }
            }
            settings.endGroup();

            break;
        }
    }

    if (rv.contains(QStringLiteral("__PPP_FILE"))) {
        QFileInfo pppFileInfo(rv[QStringLiteral("__PPP_FILE")].toString());
        QString pppFileName = QFileInfo(provisioningFileName).dir().filePath(pppFileInfo.fileName());
        QFile pppFile(pppFileName);
        if (pppFile.open(QIODevice::ReadOnly)) {
            processPppdProvisioningFile(pppFile, rv);

            pppFile.close();
        }
        rv.remove(QStringLiteral("__PPP_FILE"));
    }

    return rv;
}

QVariantMap SettingsVpnModel::processPbkProvisioningFile(QFile &provisioningFile, const QString type)
{
    QString provisioningFileName = provisioningFile.fileName();
    QSettings settings(provisioningFileName, QSettings::IniFormat);
    QStringList groups = settings.childGroups();
    QVariantMap rv;

    QString expectedVpnStrategy;

    if (type == QLatin1String("l2tp")) {
        expectedVpnStrategy = QStringLiteral("3"); // L2TP only
    } else if (type == QLatin1String("pptp")) {
        expectedVpnStrategy = QStringLiteral("1"); // PPTP only
    } // 2 would be "try PPTP, then L2TP"

    const QString expectedType = QStringLiteral("2");  // VPN
    const QString expectedDEVICE = QStringLiteral("vpn");

    for (const auto &group : groups) {
        settings.beginGroup(group);

        if (settings.value(QStringLiteral("Type")).toString() == expectedType
            && settings.value(QStringLiteral("DEVICE")).toString() == expectedDEVICE
            && settings.value(QStringLiteral("VpnStrategy")).toString() == expectedVpnStrategy) {
            rv["Host"] = settings.value(QStringLiteral("PhoneNumber"));
            rv["Name"] = group;
            break;
        }

        settings.endGroup();
    }

    return rv;
}

void SettingsVpnModel::updateBestState(VpnConnection::ConnectionState maxState)
{
    if (bestState_ != maxState) {
        bestState_ = maxState;
        emit bestStateChanged();
    }
}
