/*
 * Copyright (c) 2013 – 2019 Jolla Ltd.
 * Copyright (c) 2019 – 2020 Open Mobile Platform LLC.
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

/* Symbolic constants */
#define PROGRESS_INDETERMINATE (-1)

/* Interfaces for IP addresses */
#define USB_NETWORK_FALLBACK_INTERFACE "usb0"
#define USB_NETWORK_FALLBACK_IP "192.168.2.15"
#define WLAN_NETWORK_INTERFACE "wlan0"
#define WLAN_NETWORK_FALLBACK_INTERFACE "tether"

/* A file that is provided by the developer mode package */
#define DEVELOPER_MODE_PROVIDED_FILE "/usr/bin/devel-su"
#define DEVELOPER_MODE_PACKAGE "jolla-developer-mode"
#define DEVELOPER_MODE_PACKAGE_PRELOAD_DIR "/var/lib/jolla-developer-mode/preloaded/"

#define EMULATOR_PROVIDED_FILE "/etc/sailfishos-emulator"

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

/* Package which will move debug folder to /home/.system/usr/lib */
#define DEBUG_HOME_PACKAGE "jolla-developer-mode-home-debug-location"

static QMap<QString,QString> enumerate_network_interfaces()
{
    QMap<QString,QString> result;

    for (const QNetworkInterface &intf : QNetworkInterface::allInterfaces()) {
        for (const QNetworkAddressEntry &entry : intf.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                result[intf.name()] = entry.ip().toString();
            }
        }
    }

    return result;
}

static QString get_cached_package(const QString &version)
{
    QDir dir(DEVELOPER_MODE_PACKAGE_PRELOAD_DIR);
    QStringList filters;
    filters << QStringLiteral("%1-%2.*.rpm").arg(DEVELOPER_MODE_PACKAGE).arg(version);
    auto preloaded = dir.entryList(filters, QDir::Files, QDir::Name);
    if (preloaded.empty())
        return QString();
    return dir.absoluteFilePath(preloaded.last());
}

namespace {
    bool debugHomeFolderExists()
    {
        QDir pathDir("/home/.system/usr/lib/debug");
        if (pathDir.exists()) {
            return true;
        }
        return false;
    }
}

DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , m_usbModeDaemon(USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE, QDBusConnection::systemBus())
    , m_wlanIpAddress("-")
    , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
    , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
    , m_username(qgetenv("USER"))
    , m_developerModeEnabled(QFile::exists(DEVELOPER_MODE_PROVIDED_FILE) || QFile::exists(EMULATOR_PROVIDED_FILE))
    , m_workStatus(Idle)
    , m_workProgress(PROGRESS_INDETERMINATE)
    , m_transactionRole(PackageKit::Transaction::RoleUnknown)
    , m_transactionStatus(PackageKit::Transaction::StatusUnknown)
    , m_refreshedForInstall(false)
    , m_localInstallFailed(false)
    , m_localDeveloperModePackagePath(get_cached_package(QStringLiteral("*")))  // Initialized to possibly incompatible package
    , m_debugHomeEnabled(debugHomeFolderExists())
    , m_installationType(None)
{
    // Resolve and update local package path
    if (!m_localDeveloperModePackagePath.isEmpty()) {
        PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(DEVELOPER_MODE_PACKAGE"-preload", PackageKit::Transaction::FilterInstalled);
        connect(resolvePackage, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::reportTransactionErrorCode);
        connect(resolvePackage, &PackageKit::Transaction::package,
                this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
            Q_UNUSED(summary)
            Q_ASSERT(info == PackageKit::Transaction::InfoInstalled);
            const QString version = PackageKit::Transaction::packageVersion(packageID);
            m_localDeveloperModePackagePath = get_cached_package(version);
            if (m_localDeveloperModePackagePath.isEmpty()) {
                emit repositoryAccessRequiredChanged();
            }
            qCDebug(lcDeveloperModeLog) << "Preload package version: " << version << ", local package path: " << m_localDeveloperModePackagePath;
        });
    }

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

bool DeveloperModeSettings::developerModeEnabled() const
{
    return m_developerModeEnabled;
}

enum DeveloperModeSettings::Status DeveloperModeSettings::workStatus() const
{
    return m_workStatus;
}

int DeveloperModeSettings::workProgress() const
{
    return m_workProgress;
}

bool DeveloperModeSettings::repositoryAccessRequired() const
{
    // Aka local-install-of-developer-mode-package-is-not-possible
    return m_localInstallFailed || m_localDeveloperModePackagePath.isEmpty();
}

bool DeveloperModeSettings::debugHomeEnabled() const
{
    return m_debugHomeEnabled;
}

enum DeveloperModeSettings::InstallationType DeveloperModeSettings::installationType() const
{
    return m_installationType;
}

QString DeveloperModeSettings::packageName()
{
    if (m_installationType == DeveloperMode) {
        return DEVELOPER_MODE_PACKAGE;
    } else if (m_installationType == DebugHome) {
        return DEBUG_HOME_PACKAGE;
    } else {
        return QString();
    }
}

void DeveloperModeSettings::setInstallationType(InstallationType type)
{
    if (m_installationType != type) {
        m_installationType = type;
        emit installationTypeChanged();
    }
}

void DeveloperModeSettings::setDeveloperMode(bool enabled)
{
    if (m_developerModeEnabled != enabled) {
        if (m_workStatus != Idle) {
            qCWarning(lcDeveloperModeLog) << "DeveloperMode state change requested during activity, ignored.";
            return;
        }

        m_refreshedForInstall = false;
        setInstallationType(DeveloperMode);
        if (enabled) {
            resolveAndExecute(InstallCommand);
        } else {
            resolveAndExecute(RemoveCommand);
        }
    }
}

void DeveloperModeSettings::moveDebugToHome(bool enabled)
{
    if (m_debugHomeEnabled != enabled) {
        if (m_workStatus != Idle) {
            qCWarning(lcDeveloperModeLog) << "Debug home state change requested during activity, ignored.";
            return;
        }

        m_refreshedForInstall = false;
        setInstallationType(DebugHome);
        if (enabled) {
            resolveAndExecute(InstallCommand);
        } else {
            resolveAndExecute(RemoveCommand);
        }
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

    for (const QString &device : entries.keys()) {
        qCDebug(lcDeveloperModeLog) << "Device:" << device << "IP:" << entries[device];
    }
}

void DeveloperModeSettings::refreshPackageCacheAndInstall()
{
    m_refreshedForInstall = true;

    // Soft refresh, do not clear & reload valid cache.
    PackageKit::Transaction *refreshCache = PackageKit::Daemon::refreshCache(false);
    connect(refreshCache, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::reportTransactionErrorCode);
    connect(refreshCache, &PackageKit::Transaction::finished,
            this, [this](PackageKit::Transaction::Exit status, uint runtime) {
        qCDebug(lcDeveloperModeLog) << "Package cache updated:" << status << runtime;
        resolveAndExecute(InstallCommand); // trying again regardless of success, some repositories might be updated
    });
}

void DeveloperModeSettings::resolveAndExecute(Command command)
{
    setWorkStatus(Preparing);
    m_workProgress = 0;
    m_packageId.clear(); // might differ between installed/available

    if (command == InstallCommand && !m_localInstallFailed && !m_localDeveloperModePackagePath.isEmpty() && m_installationType == DeveloperMode) {
        // Resolve which version of developer mode package is expected
        PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(DEVELOPER_MODE_PACKAGE"-preload", PackageKit::Transaction::FilterInstalled);
        connect(resolvePackage, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::reportTransactionErrorCode);
        connect(resolvePackage, &PackageKit::Transaction::package,
                this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
            Q_UNUSED(summary)
            Q_ASSERT(info == PackageKit::Transaction::InfoInstalled);
            const QString version = PackageKit::Transaction::packageVersion(packageID);
            m_localDeveloperModePackagePath = get_cached_package(version);
            emit repositoryAccessRequiredChanged();
            qCDebug(lcDeveloperModeLog) << "Preload package version: " << version << ", local package path: " << m_localDeveloperModePackagePath;
        });

        connect(resolvePackage, &PackageKit::Transaction::finished,
                this, [this](PackageKit::Transaction::Exit status, uint runtime) {
            Q_UNUSED(runtime)
            if (status != PackageKit::Transaction::ExitSuccess || m_localDeveloperModePackagePath.isEmpty()) {
                qCDebug(lcDeveloperModeLog) << "Preloaded package not found, must use remote package";
                // No cached package => install from repos
                resolveAndExecute(InstallCommand);
            } else {
                PackageKit::Transaction *tx = PackageKit::Daemon::installFiles(QStringList() << m_localDeveloperModePackagePath);
                connectCommandSignals(tx);
                connect(tx, &PackageKit::Transaction::finished,
                        this, [this](PackageKit::Transaction::Exit status, uint runtime) {
                    if (status == PackageKit::Transaction::ExitSuccess) {
                        qCDebug(lcDeveloperModeLog) << "Developer mode installation from local package transaction done:" << status << runtime;
                        resetState();
                    } else if (status == PackageKit::Transaction::ExitFailed) {
                        qCWarning(lcDeveloperModeLog) << "Developer mode installation from local package failed, trying from repos";
                        m_localInstallFailed = true;
                        emit repositoryAccessRequiredChanged();
                        resolveAndExecute(InstallCommand);  // TODO: If repo access is not available this can not bail out
                    } // else ExitUnknown (ignored)
                });
            }
        });

    } else {
        // Install package form repos
        installAndRemove(command);
    }
}

bool DeveloperModeSettings::installAndRemove(Command command) 
{
    if (packageName().isEmpty()) {
        qCWarning(lcDeveloperModeLog) << "No installation package name set. Shouldn't happen.";
        resetState();
        return false;
    }

    PackageKit::Transaction::Filters filters;
    if (command == RemoveCommand) {
        filters = PackageKit::Transaction::FilterInstalled;
    } else {
        filters = PackageKit::Transaction::FilterNewest;
    }

    PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(packageName(), filters);

    connect(resolvePackage, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::reportTransactionErrorCode);
    connect(resolvePackage, &PackageKit::Transaction::package,
            this, [this](PackageKit::Transaction::Info info, const QString &packageId, const QString &summary) {
        qCDebug(lcDeveloperModeLog) << "Package transaction:" << info << packageId << "summary:" << summary;
        m_packageId = packageId;
    });

    connect(resolvePackage, &PackageKit::Transaction::finished,
            this, [this, command](PackageKit::Transaction::Exit status, uint runtime) {
        Q_UNUSED(runtime)

        if (status != PackageKit::Transaction::ExitSuccess || m_packageId.isEmpty()) {
            if (command == InstallCommand) {
                if (m_refreshedForInstall) {
                    qCWarning(lcDeveloperModeLog) << "Failed to install, package didn't resolve.";
                    resetState();
                } else {
                    refreshPackageCacheAndInstall(); // try once if it helps
                }
            } else if (command == RemoveCommand) {
                qCWarning(lcDeveloperModeLog) << "Removing package but package didn't resolve into anything. Shouldn't happen.";
                resetState();
            }

        } else if (command == InstallCommand) {
            PackageKit::Transaction *tx = PackageKit::Daemon::installPackage(m_packageId);
            connectCommandSignals(tx);

            if (m_refreshedForInstall) {
                connect(tx, &PackageKit::Transaction::finished,
                        this, [this](PackageKit::Transaction::Exit status, uint runtime) {
                    qCDebug(lcDeveloperModeLog) << "Installation transaction done (with refresh):" << status << runtime;
                    resetState();
                });
            } else {
                connect(tx, &PackageKit::Transaction::finished,
                        this, [this](PackageKit::Transaction::Exit status, uint runtime) {
                    if (status == PackageKit::Transaction::ExitSuccess) {
                        qCDebug(lcDeveloperModeLog) << "Installation transaction done:" << status << runtime;
                        resetState();
                    } else {
                        qCDebug(lcDeveloperModeLog) << "Installation failed, trying again after refresh";
                        refreshPackageCacheAndInstall();
                    }
                });
            }

        } else {
            PackageKit::Transaction *tx = PackageKit::Daemon::removePackage(m_packageId, true, true);
            connectCommandSignals(tx);
            connect(tx, &PackageKit::Transaction::finished,
                    this, [this](PackageKit::Transaction::Exit status, uint runtime) {
                qCDebug(lcDeveloperModeLog) << "Package removal transaction done:" << status << runtime;
                resetState();
            });
        }
    });
    return true;
}

void DeveloperModeSettings::connectCommandSignals(PackageKit::Transaction *transaction)
{
    connect(transaction, &PackageKit::Transaction::errorCode, this, &DeveloperModeSettings::reportTransactionErrorCode);
    connect(transaction, &PackageKit::Transaction::percentageChanged, this, [this, transaction]() {
        updateState(transaction->percentage(), m_transactionStatus, m_transactionRole);
    });

    connect(transaction, &PackageKit::Transaction::statusChanged, this, [this, transaction]() {
        updateState(m_workProgress, transaction->status(), m_transactionRole);
    });

    connect(transaction, &PackageKit::Transaction::roleChanged, this, [this, transaction]() {
        updateState(m_workProgress, m_transactionStatus, transaction->role());
    });
}

void DeveloperModeSettings::updateState(int percentage, PackageKit::Transaction::Status status, PackageKit::Transaction::Role role)
{
    // Expected changes from PackageKit when installing packages:
    // 1. Change to 'install packages' role or 'install files' if installing from local package file
    // 2. Status changes:
    //      setup -> refresh cache -> query -> resolve deps -> install (refer to as 'Preparing' status)
    //      -> download ('DownloadingPackages' status)
    //      -> install ('InstallingPackages' status)
    //      -> finished
    //
    // If installing from local package fails, it starts over!
    //
    // Expected changes from PackageKit when removing packages:
    // 1. Change to 'remove packages' role
    // 2. Status changes:
    //      setup -> remove -> resolve deps (refer to as 'Preparing' status)
    //      -> remove ('RemovingPackages' status)
    //      -> finished
    //
    // Notice the 'install' and 'remove' packagekit status changes occur twice.

    int progress = m_workProgress;
    DeveloperModeSettings::Status workStatus = m_workStatus;

    m_transactionRole = role;
    m_transactionStatus = status;

    // Do not update progress when finished or role is unknown.
    if (m_transactionStatus == PackageKit::Transaction::StatusFinished
            || m_transactionRole == PackageKit::Transaction::RoleUnknown) {
        return;
    }

    if (percentage >= 0 && percentage <= 100) {
        int rangeStart = 0;
        int rangeEnd = 0;
        if (m_transactionRole == PackageKit::Transaction::RoleInstallPackages
                || m_transactionRole == PackageKit::Transaction::RoleInstallFiles) {
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
                // Skip downloading when installing from local file
                if (m_transactionRole != PackageKit::Transaction::RoleInstallFiles) {
                    workStatus = DownloadingPackages;
                }
                rangeStart = 20;
                rangeEnd = 60;
                break;
            case PackageKit::Transaction::StatusInstall: // 60-100 %
                workStatus = InstallingPackages;
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
                workStatus = RemovingPackages;
                rangeStart = 20;
                rangeEnd = 100;
            }
        }
        if (rangeEnd > 0 && rangeEnd > rangeStart) {
            progress = rangeStart + ((rangeEnd - rangeStart) * (percentage / 100.0));
        }
    }

    progress = qBound(0, qMax(progress, m_workProgress), 100); // Ensure the emitted progress value never decreases.

    setWorkStatus(workStatus);

    if (m_workProgress != progress) {
        m_workProgress = progress;
        emit workProgressChanged();
    }
}

void DeveloperModeSettings::resetState()
{
    if (m_installationType == DeveloperMode) {
        bool enabled = QFile::exists(DEVELOPER_MODE_PROVIDED_FILE) || QFile::exists(EMULATOR_PROVIDED_FILE);
        if (m_developerModeEnabled != enabled) {
            m_developerModeEnabled = enabled;
            emit developerModeEnabledChanged();
        }
    } else if (m_installationType == DebugHome) {
        if (m_debugHomeEnabled != debugHomeFolderExists()) {
            m_debugHomeEnabled = debugHomeFolderExists();
            emit debugHomeEnabledChanged();
        }
    }

    setWorkStatus(Idle);
    setInstallationType(None);

    if (m_workProgress != PROGRESS_INDETERMINATE) {
        m_workProgress = PROGRESS_INDETERMINATE;
        emit workProgressChanged();
    }
}

void DeveloperModeSettings::setWorkStatus(DeveloperModeSettings::Status status)
{
    if (m_workStatus != status) {
        m_workStatus = status;
        emit workStatusChanged();
    }
}

void DeveloperModeSettings::reportTransactionErrorCode(PackageKit::Transaction::Error code, const QString &details)
{
    qCWarning(lcDeveloperModeLog) << "Transaction error:" << code << details;
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
