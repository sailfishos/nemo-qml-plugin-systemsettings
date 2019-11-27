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

#include <QRegularExpression>
#include <QStandardPaths>
#include <QDataStream>
#include <QCryptographicHash>
#include <QQmlEngine>
#include <QDir>
#include "logging_p.h"
#include "vpnmanager.h"

#include "settingsvpnmodel.h"

namespace {

const auto defaultDomain = QStringLiteral("sailfishos.org");
const auto legacyDefaultDomain(QStringLiteral("merproject.org"));

int numericValue(VpnConnection::ConnectionState state)
{
    return (state == VpnConnection::Ready ? 3 :
                (state == VpnConnection::Configuration ? 2 : 0));
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
        if (type == QString("openvpn")) {
            rv = processOpenVpnProvisioningFile(provisioningFile);
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

void SettingsVpnModel::updateBestState(VpnConnection::ConnectionState maxState)
{
    if (bestState_ != maxState) {
        bestState_ = maxState;
        emit bestStateChanged();
    }
}
