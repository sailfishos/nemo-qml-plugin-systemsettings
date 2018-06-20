/*
 * Copyright (C) 2013-2018 Jolla Ltd.
 * Contact: Thomas Perl <thomas.perl@jollamobile.com>
 * Contact: Raine Makelainen <raine.makelainen@jolla.com>
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
#include "logging_p.h"

#include <QFile>
#include <QDir>
#include <QDBusReply>
#include <QNetworkInterface>
#include <transaction.h>

#include <getdef.h>
#include <pwd.h>

/* Symbolic constants */
#define PROGRESS_INDETERMINATE (-1)

/* Interfaces for IP addresses */
#define USB_NETWORK_FALLBACK_INTERFACE "usb0"
#define USB_NETWORK_FALLBACK_IP "192.168.2.15"
#define WLAN_NETWORK_INTERFACE "wlan0"
#define WLAN_NETWORK_FALLBACK_INTERFACE "tether"

/* A file that is provided by the developer mode package */
#define DEVELOPER_MODE_PROVIDED_FILE "/usr/bin/devel-su"

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

static QMap<QString,QString> enumerate_network_interfaces()
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

DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , m_usbModeDaemon(USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE, QDBusConnection::systemBus())
    , m_wlanIpAddress("-")
    , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
    , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
    , m_username("nemo")
    , m_developerModeEnabled(QFile::exists(DEVELOPER_MODE_PROVIDED_FILE))
    , m_workerStatus(Idle)
    , m_workerProgress(PROGRESS_INDETERMINATE)
    , m_transactionRole(PackageKit::Transaction::RoleUnknown)
    , m_transactionStatus(PackageKit::Transaction::StatusUnknown)
    , m_cacheUpdated(false)
{
    int uid = getdef_num("UID_MIN", -1);
    struct passwd *pwd;
    if ((pwd = getpwuid(uid)) != NULL) {
        m_username = QString(pwd->pw_name);
    } else {
        qCWarning(lcDeveloperModeLog) << "Failed to return username using getpwuid()";
    }

    checkDeveloperModeStatus(true);
    refresh();

    // TODO: Watch WLAN / USB IP addresses for changes
    // TODO: Watch package manager for changes to developer mode
}

DeveloperModeSettings::~DeveloperModeSettings()
{
}

QString DeveloperModeSettings::wlanIpAddress() const
{
    return m_wlanIpAddress;
}

QString DeveloperModeSettings::usbIpAddress() const
{
    return m_usbIpAddress;
}

QString DeveloperModeSettings::username() const
{
    return m_username;
}

bool DeveloperModeSettings::developerModeAvailable() const
{
    return m_developerModeEnabled || !m_developerModePackageId.isEmpty();
}

bool DeveloperModeSettings::developerModeEnabled() const
{
    return m_developerModeEnabled;
}

enum DeveloperModeSettings::Status DeveloperModeSettings::workerStatus() const
{
    return m_workerStatus;
}

int DeveloperModeSettings::workerProgress() const
{
    return m_workerProgress;
}

void DeveloperModeSettings::setDeveloperMode(bool enabled)
{
    if (m_developerModeEnabled != enabled) {
        m_workerStatus = Preparing;
        if (enabled) {
            resolveDeveloperModePackageId(InstallCommand);
        } else {
            resolveDeveloperModePackageId(RemoveCommand);
        }

        emit workerStatusChanged();
    }
}

void DeveloperModeSettings::setUsbIpAddress(const QString &usbIpAddress)
{
    if (m_usbIpAddress != usbIpAddress) {
        usbModedSetConfig(USB_MODED_CONFIG_IP, usbIpAddress);
        m_usbIpAddress = usbIpAddress;
        emit usbIpAddressChanged();
    }
}

void DeveloperModeSettings::refresh()
{
    /* Retrieve network configuration from usb_moded */
    m_usbInterface = usbModedGetConfig(USB_MODED_CONFIG_INTERFACE, USB_NETWORK_FALLBACK_INTERFACE);
    QString usbIp = usbModedGetConfig(USB_MODED_CONFIG_IP, USB_NETWORK_FALLBACK_IP);

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
        qCDebug(lcDeveloperModeLog) << "Device:" << device
                                    << "IP:" << ip;
    }
}

void DeveloperModeSettings::checkDeveloperModeStatus()
{
    checkDeveloperModeStatus(false);
}

void DeveloperModeSettings::checkDeveloperModeStatus(bool initial)
{
    if (!m_developerModeEnabled) {
        Status oldStatus = m_workerStatus;
        if (oldStatus != Idle && oldStatus != InitialCheckingStatus) {
            qCWarning(lcDeveloperModeLog) << Q_FUNC_INFO << "called while action ongoing, ignoring.";
            return;
        }

        m_workerStatus = initial ? InitialCheckingStatus : CheckingStatus;
        if (oldStatus == Idle) {
            resolveDeveloperModePackageId(NoCommand);
        }

        if (oldStatus != m_workerStatus) {
            emit workerStatusChanged();
        }
    }
}

void DeveloperModeSettings::refreshPackageCache()
{
    // Soft refresh, do not clear & reload valid cache.
    PackageKit::Transaction *refreshCache = PackageKit::Daemon::refreshCache(false);
    connect(refreshCache, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::transactionErrorCode);
    connect(refreshCache, &PackageKit::Transaction::finished, this, [this](PackageKit::Transaction::Exit status, uint runtime) {
        qCDebug(lcDeveloperModeLog) << "Package cache updated:" << status << runtime;
        m_cacheUpdated = (status == PackageKit::Transaction::ExitSuccess);
        if (m_cacheUpdated) {
            emit packageCacheUpdated();
        }
        transactionFinished(status, runtime);
    });
}

void DeveloperModeSettings::resolveDeveloperModePackageId(Command command)
{
    if (!m_cacheUpdated) {
        refreshPackageCache();
        connect(this, &DeveloperModeSettings::packageCacheUpdated, this, [this, command]() {
            resolveDeveloperModePackageId(command);
        });
    } else {
        PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(QStringLiteral("jolla-developer-mode"));
        connect(resolvePackage, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::transactionErrorCode);
        connect(resolvePackage, &PackageKit::Transaction::package, this, &DeveloperModeSettings::transactionPackage);
        connect(resolvePackage, &PackageKit::Transaction::finished, this, [this, command](PackageKit::Transaction::Exit status, uint runtime) {
            transactionFinished(status, runtime);
            switch (command) {
            case InstallCommand:
                connectCommandSignals(PackageKit::Daemon::installPackage(m_developerModePackageId));
                break;
            case RemoveCommand:
                connectCommandSignals(PackageKit::Daemon::removePackage(m_developerModePackageId, true, true));
                break;
            case NoCommand:
                // Nothing to do
                break;
            default:
                break;
            }
        });
    }
}

void DeveloperModeSettings::connectCommandSignals(PackageKit::Transaction *transaction)
{
    connect(transaction, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::transactionErrorCode);
    connect(transaction, &PackageKit::Transaction::finished, this, &DeveloperModeSettings::transactionFinished);
    connect(transaction, &PackageKit::Transaction::percentageChanged, this, [this, transaction]() {
        updateState(transaction->percentage(), m_transactionStatus, m_transactionRole);
    });

    connect(transaction, &PackageKit::Transaction::statusChanged, this, [this, transaction]() {
        updateState(m_workerProgress, transaction->status(), m_transactionRole);
    });

    connect(transaction, &PackageKit::Transaction::roleChanged, this, [this, transaction]() {
        updateState(m_workerProgress, m_transactionStatus, transaction->role());
    });
}

void DeveloperModeSettings::transactionPackage(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary)
{
    qCDebug(lcDeveloperModeLog) << "Package transaction:" << info << packageId << "summary:" << summary;
    m_developerModePackageId = packageId;

    if (!m_developerModePackageId.isEmpty()) {
        emit developerModeAvailableChanged();
    }
}

void DeveloperModeSettings::updateState(int percentage, PackageKit::Transaction::Status status, PackageKit::Transaction::Role role)
{
    // Do not update progress when finished.
    if (status == PackageKit::Transaction::StatusFinished) {
        return;
    }

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

    int progress = m_workerProgress;
    DeveloperModeSettings::Status workerStatus = m_workerStatus;

    m_transactionRole = role;
    m_transactionStatus = status;

    if (percentage >= 0 && percentage <= 100) {
        int rangeStart = 0;
        int rangeEnd = 0;
        if (m_transactionRole == PackageKit::Transaction::RoleInstallPackages) {
            switch (m_transactionStatus) {
            case PackageKit::Transaction::StatusRefreshCache:   // 0-10 %
                rangeStart = 0;
                rangeEnd = 10;
                break;
            case PackageKit::Transaction::StatusQuery: // fall through; packagekit progress changes 0-100 over query->resolve stages
            case PackageKit::Transaction::StatusDepResolve:    // 10-20 %
                rangeStart = 10;
                rangeEnd = 20;
                break;
            case PackageKit::Transaction::StatusDownload:    // 20-60 %
                workerStatus = DownloadingPackages;
                rangeStart = 20;
                rangeEnd = 60;
                break;
            case PackageKit::Transaction::StatusInstall: // 60-100 %
                workerStatus = InstallingPackages;
                rangeStart = 60;
                rangeEnd = 100;
                break;
            default:
                break;
            }
        } else if (m_transactionRole == PackageKit::Transaction::RoleRemovePackages) {
            if (m_transactionStatus == PackageKit::Transaction::StatusSetup) {
                // Let the setup to be bound between 0-20 %
                rangeStart = 0;
                rangeEnd = 20;
            } else {    // 20-100 %
                workerStatus = RemovingPackages;
                rangeStart = 20;
                rangeEnd = 100;
            }
        }
        if (rangeEnd > 0 && rangeEnd > rangeStart) {
            progress = rangeStart + ((rangeEnd - rangeStart) * (percentage / 100.0));
        }
    }

    progress = qBound(0, qMax(progress, m_workerProgress), 100); // Ensure the emitted progress value never decreases.

    if (m_workerStatus != workerStatus) {
        m_workerStatus = workerStatus;
        emit workerStatusChanged();
    }
    if (m_workerProgress != progress) {
        m_workerProgress = progress;
        emit workerProgressChanged();
    }
}

void DeveloperModeSettings::transactionErrorCode(PackageKit::Transaction::Error code, const QString &details)
{
    qCWarning(lcDeveloperModeLog) << "Transaction error:" << code << details;
}

void DeveloperModeSettings::transactionFinished(PackageKit::Transaction::Exit status, uint runtime)
{
    qCDebug(lcDeveloperModeLog) << "Transaction finished:" << status << runtime;

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

QString DeveloperModeSettings::usbModedGetConfig(const QString &key, const QString &fallback)
{
    QString value = fallback;

    QDBusMessage msg = m_usbModeDaemon.call(USB_MODED_GET_NET_CONFIG, key);
    QList<QVariant> result = msg.arguments();
    if (result[0].toString() == key && result.size() == 2) {
        value = result[1].toString();
    }

    return value;
}

void DeveloperModeSettings::usbModedSetConfig(const QString &key, const QString &value)
{
    m_usbModeDaemon.call(USB_MODED_SET_NET_CONFIG, key, value);
}
