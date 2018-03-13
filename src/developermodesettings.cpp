/*
 * Copyright (C) 2013 Jolla Ltd.
 * Contact: Thomas Perl <thomas.perl@jollamobile.com>
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

#include "developermodesettings.h"

#if 0
/* libsailfishaccounts */
#include <accountmanager.h>
#include <provider.h>
#include <service.h>
#endif

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDBusReply>
#include <QNetworkInterface>

#include <getdef.h>
#include <pwd.h>

/* Symbolic constants */
#define PROGRESS_INDETERMINATE (-1)

/* Interfaces for IP addresses */
#define USB_NETWORK_FALLBACK_INTERFACE "usb0"
#define USB_NETWORK_FALLBACK_IP "192.168.2.15"
#define WLAN_NETWORK_INTERFACE "wlan0"
#define WLAN_NETWORK_FALLBACK_INTERFACE "tether"

/* Developer mode package */
#define DEVELOPER_MODE_PACKAGE "jolla-developer-mode"

/* A file that is provided by the developer mode package */
#define DEVELOPER_MODE_PROVIDED_FILE "/usr/bin/devel-su"

/* The service name for developer mode services offered by an account provider */
#define DEVELOPER_MODE_ACCOUNT_SERVICE "developermode"

/* D-Bus service */
#define PACKAGEKIT_SERVICE "org.freedesktop.PackageKit"
#define PACKAGEKIT_PATH "/org/freedesktop/PackageKit"
#define PACKAGEKIT_INTERFACE "org.freedesktop.PackageKit"
#define PACKAGEKIT_TRANSACTION_INTERFACE "org.freedesktop.PackageKit.Transaction"

/* D-Bus method names */
#define PACKAGEKIT_CREATETRANSACTION "CreateTransaction"
#define PACKAGEKIT_TRANSACTION_RESOLVE "Resolve"
#define PACKAGEKIT_TRANSACTION_CANCEL "Cancel"
#define PACKAGEKIT_TRANSACTION_INSTALLPACKAGES "InstallPackages"
#define PACKAGEKIT_TRANSACTION_REMOVEPACKAGES "RemovePackages"

/* D-Bus signal names */
#define PACKAGEKIT_TRANSACTION_PACKAGE "Package"
#define PACKAGEKIT_TRANSACTION_ERRORCODE "ErrorCode"
#define PACKAGEKIT_TRANSACTION_FINISHED "Finished"

/* D-Bus property names */
#define PACKAGEKIT_TRANSACTION_PERCENTAGE "Percentage"
#define PACKAGEKIT_TRANSACTION_STATUS "Status"
#define PACKAGEKIT_TRANSACTION_ROLE "Role"

/* D-Bus service */
#define USB_MODED_SERVICE "com.meego.usb_moded"
#define USB_MODED_PATH "/com/meego/usb_moded"
#define USB_MODED_INTERFACE "com.meego.usb_moded"

/* D-Bus method names */
#define USB_MODED_GET_NET_CONFIG "get_net_config"
#define USB_MODED_SET_NET_CONFIG "net_config"

/* USB Mode Daemon network configuration properties */
#define USB_MODED_CONFIG_IP "ip"
#define USB_MODED_CONFIG_INTERFACE "interface"

#define DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define DBUS_PROPERTIES_CHANGED "PropertiesChanged"

enum TransactionRole {
    TransactionRoleInstallPackages = 11,
    TransactionRoleRemovePackages = 14
};

enum TransactionStatus {
    TransactionStatusSetup = 2,
    TransactionStatusQuery = 4,
    TransactionStatusRemove = 6,
    TransactionStatusRefreshCache = 7,
    TransactionStatusDownload = 8,
    TransactionStatusInstall = 9,
    TransactionStatusResolveDeps = 13,
    TransactionStatusFinished = 18
};

static QMap<QString,QString>
enumerate_network_interfaces()
{
    QMap<QString,QString> result;

    foreach (const QNetworkInterface &intf, QNetworkInterface::allInterfaces()) {
        foreach (const QNetworkAddressEntry &entry, intf.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                result[intf.name()] = entry.ip().toString();
            }
        }
    }

    return result;
}

static inline QString
usb_moded_get_config(QDBusInterface &usb, QString key, QString fallback)
{
    QString value = fallback;

    QDBusMessage msg = usb.call(USB_MODED_GET_NET_CONFIG, key);
    QList<QVariant> result = msg.arguments();
    if (result[0].toString() == key && result.size() == 2) {
        value = result[1].toString();
    }

    return value;
}

static inline void
usb_moded_set_config(QDBusInterface &usb, QString key, QString value)
{
    usb.call(USB_MODED_SET_NET_CONFIG, key, value);
}


DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , m_usbModeDaemon(USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE,
            QDBusConnection::systemBus())
//    , m_accountManager(new AccountManager(this))
    , m_pendingPackageKitCall(nullptr)
    , m_packageKitCommand(nullptr)
    , m_wlanIpAddress("-")
    , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
    , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
    , m_username("nemo")
    , m_developerModeEnabled(QFile::exists(DEVELOPER_MODE_PROVIDED_FILE))
    , m_remoteLoginEnabled(false) // TODO: Read (from password manager?)
    , m_workerStatus(Idle)
    , m_workerProgress(PROGRESS_INDETERMINATE)
    , m_transactionRole(0)
    , m_transactionStatus(0)
{
    int uid = getdef_num("UID_MIN", -1);
    struct passwd *pwd;
    if ((pwd = getpwuid(uid)) != NULL) {
        m_username = QString(pwd->pw_name);
    } else {
        qWarning() << "Failed to return username using getpwuid()";
    }

#if 0
    updateAccountProvider();
    connect(m_accountManager, &AccountManager::providerNamesChanged,
            this, &DeveloperModeSettings::updateAccountProvider);
#endif

    if (!m_developerModeEnabled) {
        m_workerStatus = CheckingStatus;
        executePackageKitCommand(&DeveloperModeSettings::resolvePackageId, DEVELOPER_MODE_PACKAGE);
    }

    refresh();

    // TODO: Watch WLAN / USB IP addresses for changes
    // TODO: Watch package manager for changes to developer mode
}

DeveloperModeSettings::~DeveloperModeSettings()
{
    delete m_pendingPackageKitCall;
}

QString
DeveloperModeSettings::wlanIpAddress() const
{
    return m_wlanIpAddress;
}

QString
DeveloperModeSettings::usbIpAddress() const
{
    return m_usbIpAddress;
}

QString
DeveloperModeSettings::username() const
{
    return m_username;
}

QString DeveloperModeSettings::developerModeAccountProvider() const
{
    return m_developerModeAccountProvider;
}

bool DeveloperModeSettings::developerModeAvailable() const
{
    return m_developerModeEnabled || !m_developerModePackageId.isEmpty();
}

bool
DeveloperModeSettings::developerModeEnabled() const
{
    return m_developerModeEnabled;
}

bool
DeveloperModeSettings::remoteLoginEnabled() const
{
    return m_remoteLoginEnabled;
}

enum DeveloperModeSettings::Status
DeveloperModeSettings::workerStatus() const
{
    return m_workerStatus;
}

int
DeveloperModeSettings::workerProgress() const
{
    return m_workerProgress;
}

void
DeveloperModeSettings::setDeveloperMode(bool enabled)
{
    if (m_developerModeEnabled != enabled
            && !m_pendingPackageKitCall
            && m_packageKitTransaction.path().isEmpty()) {
        m_workerStatus = Preparing;
        if (enabled) {
            m_packageKitCommand = &DeveloperModeSettings::installPackage;
        } else {
            m_packageKitCommand = &DeveloperModeSettings::removePackage;
        }

        // Always resolve the developer-mode package before doing an install/remove, even if it was
        // previously resolved; otherwise, the install/remove finishes prematurely before packages
        // are actually downloaded/removed.
        executePackageKitCommand(&DeveloperModeSettings::resolvePackageId, DEVELOPER_MODE_PACKAGE);
        emit workerStatusChanged();
    }
}

void
DeveloperModeSettings::setRemoteLogin(bool enabled)
{
    if (m_remoteLoginEnabled != enabled) {
        m_remoteLoginEnabled = enabled;
        emit remoteLoginEnabledChanged();
    }
}

void
DeveloperModeSettings::setUsbIpAddress(const QString &usbIpAddress)
{
    if (m_usbIpAddress != usbIpAddress) {
        usb_moded_set_config(m_usbModeDaemon, USB_MODED_CONFIG_IP, usbIpAddress);
        m_usbIpAddress = usbIpAddress;
        emit usbIpAddressChanged();
    }
}

void
DeveloperModeSettings::refresh()
{
    /* Retrieve network configuration from usb_moded */
    m_usbInterface = usb_moded_get_config(m_usbModeDaemon,
            USB_MODED_CONFIG_INTERFACE, USB_NETWORK_FALLBACK_INTERFACE);
    QString usbIp = usb_moded_get_config(m_usbModeDaemon,
            USB_MODED_CONFIG_IP, USB_NETWORK_FALLBACK_IP);
    if (usbIp != m_usbIpAddress) {
        m_usbIpAddress = usbIp;
        emit usbIpAddressChanged();
    }

    /* Retrieve network configuration from interfaces */
    QMap<QString,QString> entries = enumerate_network_interfaces();

    if (entries.contains(m_usbInterface)) {
        QString ip = entries[m_usbInterface];
        if (m_usbIpAddress != ip) {
            m_usbIpAddress = ip;
            emit usbIpAddressChanged();
        }
    }

    if (entries.contains(WLAN_NETWORK_INTERFACE)) {
        QString ip = entries[WLAN_NETWORK_INTERFACE];
        if (m_wlanIpAddress != ip) {
            m_wlanIpAddress = ip;
            emit wlanIpAddressChanged();
        }
    } else if (entries.contains(WLAN_NETWORK_FALLBACK_INTERFACE)) {
        // If the WLAN network interface does not have an IP address,
        // but there is a "tether" interface that does have an IP, assume
        // it is the WLAN interface in tethering mode, and use its IP.
        QString ip = entries[WLAN_NETWORK_FALLBACK_INTERFACE];
        if (m_wlanIpAddress != ip) {
            m_wlanIpAddress = ip;
            emit wlanIpAddressChanged();
        }
    }

    foreach (const QString &device, entries.keys()) {
        QString ip = entries[device];
        qDebug() << "Device:" << device
                 << "IP:" << ip;
    }
}

static QDBusPendingCallWatcher *packageKitCall(
        const QString &path,
        const QString &interface,
        const QString &method,
        const QVariantList &arguments = QVariantList())
{
    QDBusMessage message = QDBusMessage::createMethodCall(PACKAGEKIT_SERVICE, path, interface, method);
    message.setArguments(arguments);

    return new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message));
}

static QDBusPendingCallWatcher *packageKitCall(
        const QString &method,
        const QVariantList &arguments = QVariantList())
{
    return packageKitCall(PACKAGEKIT_PATH, PACKAGEKIT_INTERFACE, method, arguments);
}

static QDBusPendingCallWatcher *packageKitTransactionCall(
        const QDBusObjectPath &path,
        const QString &method,
        const QVariantList &arguments = QVariantList())
{
    return packageKitCall(path.path(), PACKAGEKIT_TRANSACTION_INTERFACE, method, arguments);
}

QDBusPendingCallWatcher *DeveloperModeSettings::resolvePackageId(const QString &packageName)
{
    connectTransactionSignal(
                PACKAGEKIT_TRANSACTION_PACKAGE,
                SLOT(transactionPackage(uint,QString)));

    connectPropertiesChanged();

    connectTransactionSignal(
                PACKAGEKIT_TRANSACTION_FINISHED,
                SLOT(transactionFinished(uint,uint)));

    return packageKitTransactionCall(
                m_packageKitTransaction,
                PACKAGEKIT_TRANSACTION_RESOLVE,
                QVariantList() << quint64() << (QStringList() << packageName));
}

QDBusPendingCallWatcher *DeveloperModeSettings::installPackage(const QString &packageId)
{
    connectPropertiesChanged();

    connectTransactionSignal(
                PACKAGEKIT_TRANSACTION_FINISHED,
                SLOT(transactionFinished(uint,uint)));

    return packageKitTransactionCall(m_packageKitTransaction, PACKAGEKIT_TRANSACTION_INSTALLPACKAGES, QVariantList()
                << QVariant::fromValue(quint64())
                << (QStringList() << packageId));
}

QDBusPendingCallWatcher *DeveloperModeSettings::removePackage(const QString &packageId)
{
    connectPropertiesChanged();

    connectTransactionSignal(
                PACKAGEKIT_TRANSACTION_FINISHED,
                SLOT(transactionFinished(uint,uint)));

    return packageKitTransactionCall(m_packageKitTransaction, PACKAGEKIT_TRANSACTION_REMOVEPACKAGES, QVariantList()
                << QVariant::fromValue(quint64())
                << (QStringList() << packageId)
                << true
                << true);
}

void DeveloperModeSettings::connectTransactionSignal(const QString &name, const char *slot)
{
    QDBusConnection::systemBus().connect(
                PACKAGEKIT_SERVICE,
                m_packageKitTransaction.path(),
                PACKAGEKIT_TRANSACTION_INTERFACE,
                name,
                this,
                slot);
}

void DeveloperModeSettings::connectPropertiesChanged()
{
    QDBusConnection::systemBus().connect(
                PACKAGEKIT_SERVICE,
                m_packageKitTransaction.path(),
                DBUS_PROPERTIES_INTERFACE,
                DBUS_PROPERTIES_CHANGED,
                this,
                SLOT(transactionPropertiesChanged(QString,QVariantMap,QStringList)));
}

void DeveloperModeSettings::executePackageKitCommand(
        QDBusPendingCallWatcher *(DeveloperModeSettings::*command)(const QString &),
        const QString &argument)
{
    Q_ASSERT(!m_pendingPackageKitCall);
    m_pendingPackageKitCall = packageKitCall(PACKAGEKIT_CREATETRANSACTION);

    connect(m_pendingPackageKitCall,
                &QDBusPendingCallWatcher::finished,
                this,
                [this, command, argument](QDBusPendingCallWatcher *watcher) {
        Q_ASSERT(m_pendingPackageKitCall == watcher);
        watcher->deleteLater();
        m_pendingPackageKitCall = nullptr;

        QDBusReply<QDBusObjectPath> reply = *watcher;
        if (reply.isValid()) {
            m_packageKitTransaction = reply.value();

            connectTransactionSignal(
                        PACKAGEKIT_TRANSACTION_ERRORCODE,
                        SLOT(transactionErrorCode(uint,QString)));

            m_pendingPackageKitCall = (this->*command)(argument);

            connect(m_pendingPackageKitCall,
                        &QDBusPendingCallWatcher::finished,
                        this,
                        [this](QDBusPendingCallWatcher *watcher) {
                Q_ASSERT(m_pendingPackageKitCall == watcher);
                watcher->deleteLater();
                m_pendingPackageKitCall = nullptr;

                QDBusReply<void> reply = *watcher;
                if (!reply.isValid()) {
                    qWarning() << "Failed to call PackageKit method" << reply.error().message();

                    m_workerStatus = Idle;
                    emit workerStatusChanged();
                }
            });
        } else {
            qWarning() << "Failed to create PackageKit transaction" << reply.error().message();

            m_workerStatus = Idle;
            emit workerStatusChanged();
        }
    });
}

void DeveloperModeSettings::transactionPackage(uint, const QString &packageId)
{
    Q_ASSERT(!m_pendingPackageKitCall);

    m_packageKitTransaction = QDBusObjectPath();

    if (m_packageKitCommand) {
        executePackageKitCommand(m_packageKitCommand, packageId);
        m_packageKitCommand = nullptr;
    }

    m_developerModePackageId = packageId;

    if (!m_developerModeEnabled) {
        emit developerModeAvailableChanged();
    }
}

void DeveloperModeSettings::transactionPropertiesChanged(
        const QString &interface, const QVariantMap &changed, const QStringList &)
{
    qDebug() << "properties changed" << interface << changed;

    // Expected changes from PackageKit when installing packages:
    // 1. Change to 'resolve' role
    // 2. Status changes: setup -> query -> finished
    // 3. Change to 'install packages' role
    // 4. Status changes:
    //      setup -> refresh cache -> query -> resolve deps -> install (refer to as 'Preparing' status)
    //      -> download ('DownloadingPackages' status)
    //      -> install ('InstallingPackages' status)
    //      -> finished
    //
    // Expected changes from PackageKit when removing packages:
    // 1. Change to 'resolve' role
    // 2. Status changes: setup -> query -> finished
    // 3. Change to 'remove packages' role
    // 4. Status changes:
    //      setup -> remove -> resolve deps (refer to as 'Preparing' status)
    //      -> remove ('RemovingPackages' status)
    //      -> finished
    //
    // Notice the 'install' and 'remove' packagekit status changes occur twice.

    if (interface != PACKAGEKIT_TRANSACTION_INTERFACE) {
        return;
    }

    int progress = m_workerProgress;
    DeveloperModeSettings::Status workerStatus = m_workerStatus;

    auto it = changed.find(PACKAGEKIT_TRANSACTION_ROLE);
    if (it != changed.end()) {
        m_transactionRole = it->toInt();
        switch (m_transactionRole) {
        case TransactionRoleInstallPackages:
        case TransactionRoleRemovePackages:
            progress = 0;
            m_statusChanges.clear();
            break;
        default:
            progress = PROGRESS_INDETERMINATE;
            break;
        }
    }

    it = changed.find(PACKAGEKIT_TRANSACTION_STATUS);
    if (it != changed.end()) {
        m_transactionStatus = it->toInt();
        m_statusChanges.append(m_transactionStatus);
    }

    it = changed.find(PACKAGEKIT_TRANSACTION_PERCENTAGE);
    if (it != changed.end() && it->toInt() > 0 && it->toInt() <= 100) {
        int rangeStart = 0;
        int rangeEnd = 0;
        if (m_transactionRole == TransactionRoleInstallPackages) {
            switch (m_transactionStatus) {
            case TransactionStatusRefreshCache:   // 0-10 %
                rangeStart = 0;
                rangeEnd = 10;
                break;
            case TransactionStatusQuery: // fall through; packagekit progress changes 0-100 over query->resolve stages
            case TransactionStatusResolveDeps:    // 10-20 %
                rangeStart = 10;
                rangeEnd = 20;
                break;
            case TransactionStatusDownload:    // 20-60 %
                workerStatus = DownloadingPackages;
                rangeStart = 20;
                rangeEnd = 60;
                break;
            case TransactionStatusInstall: // 60-100 %
                if (m_statusChanges.count(TransactionStatusInstall) > 1) {
                    workerStatus = InstallingPackages;
                    rangeStart = 60;
                    rangeEnd = 100;
                }
                break;
            default:
                break;
            }
        } else if (m_transactionRole == TransactionRoleRemovePackages) {
            if (m_statusChanges.count(TransactionStatusRemove) <= 1) {  // 0-20 %
                // The final remove operation hasn't started, so this progress value must be for
                // the setup/resolve stages.
                rangeStart = 0;
                rangeEnd = 20;
            } else {    // 20-100 %
                workerStatus = RemovingPackages;
                rangeStart = 20;
                rangeEnd = 100;
            }
        }
        if (rangeEnd > 0 && rangeEnd > rangeStart) {
            progress = rangeStart + ((rangeEnd - rangeStart) * (it->toInt() / 100.0));
        }
    }

    progress = m_workerProgress == PROGRESS_INDETERMINATE
            ? progress
            : qMax(progress, m_workerProgress); // Ensure the emitted progress value never decreases.

    if (progress > 100) {
        progress = PROGRESS_INDETERMINATE;
    }

    if (m_workerStatus != workerStatus) {
        m_workerStatus = workerStatus;
        emit workerStatusChanged();
    }
    if (m_workerProgress != progress) {
        m_workerProgress = progress;
        emit workerProgressChanged();
    }
}

void DeveloperModeSettings::transactionErrorCode(uint code, const QString &message)
{
    qWarning() << "PackageKit error" << code << message;

    QDBusConnection::systemBus().call(QDBusMessage::createMethodCall(
                PACKAGEKIT_SERVICE,
                m_packageKitTransaction.path(),
                PACKAGEKIT_TRANSACTION_INTERFACE,
                PACKAGEKIT_TRANSACTION_CANCEL), QDBus::NoBlock);
}

void DeveloperModeSettings::transactionFinished(uint, uint)
{
    m_packageKitTransaction = QDBusObjectPath();

    const bool enabled = m_developerModeEnabled;
    m_developerModeEnabled = QFile::exists(DEVELOPER_MODE_PROVIDED_FILE);

    m_workerStatus = Idle;
    m_workerProgress = PROGRESS_INDETERMINATE;

    if (m_developerModeEnabled != enabled) {
        emit developerModeEnabledChanged();
    }
    emit workerStatusChanged();
    emit workerProgressChanged();
}

#if 0
void DeveloperModeSettings::updateAccountProvider()
{
    // Find all account providers with the developer-mode service. If m_developerModeAccountProvider
    // is already set to the name of one of these providers, just return. Otherwise, set it to the
    // first provider found with a matching service.
    QList<Provider *> accountProviders;
    for (const QString &providerName : m_accountManager->providerNames()) {
        if (Provider *accountProvider = m_accountManager->provider(providerName)) {
            for (const QString &serviceName : accountProvider->serviceNames()) {
                if (Service *accountService = m_accountManager->service(serviceName)) {
                    if (accountService->serviceType() == DEVELOPER_MODE_ACCOUNT_SERVICE) {
                        if (m_developerModeAccountProvider == providerName) {
                            return;
                        } else {
                            accountProviders.append(accountProvider);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (!accountProviders.isEmpty()) {
        if (m_developerModeAccountProvider != accountProviders.first()->name()) {
            m_developerModeAccountProvider = accountProviders.first()->name();
            emit developerModeAccountProviderChanged();
        }
    }
}
#endif
