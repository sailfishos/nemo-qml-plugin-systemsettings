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

#include <daemon.h>
#include <nemo-dbus/connection.h>
#include <nemo-dbus/interface.h>

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

static QMap<QString, QString> enumerate_network_interfaces()
{
    QMap<QString, QString> result;

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

class DeveloperModeSettingsPrivate: public QObject
{
    Q_OBJECT
public:
    enum Command {
        InstallCommand,
        RemoveCommand
    };

    DeveloperModeSettingsPrivate(DeveloperModeSettings *parent)
        : QObject(parent)
        , q(parent)
        , m_connection(QDBusConnection::systemBus())
        , m_usbModeDaemon(this, m_connection, USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE)
        , m_wlanIpAddress("-")
        , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
        , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
        , m_username(qgetenv("USER"))
        , m_developerModeEnabled(QFile::exists(DEVELOPER_MODE_PROVIDED_FILE) || QFile::exists(EMULATOR_PROVIDED_FILE))
        , m_workStatus(DeveloperModeSettings::Idle)
        , m_workProgress(PROGRESS_INDETERMINATE)
        , m_transactionRole(PackageKit::Transaction::RoleUnknown)
        , m_transactionStatus(PackageKit::Transaction::StatusUnknown)
        , m_refreshedForInstall(false)
        , m_localInstallFailed(false)
        , m_localDeveloperModePackagePath(get_cached_package(QStringLiteral("*")))  // Initialized to possibly incompatible package
        , m_debugHomeEnabled(debugHomeFolderExists())
        , m_installationType(DeveloperModeSettings::None)
    {}

    ~DeveloperModeSettingsPrivate() {}

    void reportTransactionErrorCode(PackageKit::Transaction::Error code, const QString &details);
    void updateState(int percentage, PackageKit::Transaction::Status status, PackageKit::Transaction::Role role);

    void resetState();
    void setWorkStatus(DeveloperModeSettings::Status status);
    void refreshPackageCacheAndInstall();
    void resolveAndExecute(Command command);
    bool installAndRemove(Command command);
    void connectCommandSignals(PackageKit::Transaction *transaction);
    void setInstallationType(DeveloperModeSettings::InstallationType type);

    QString usbModedGetConfig(const QString &key, const QString &fallback);
    void usbModedSetConfig(const QString &key, const QString &value);

    DeveloperModeSettings *q;
    NemoDBus::Connection m_connection;
    NemoDBus::Interface m_usbModeDaemon;
    QString m_wlanIpAddress;
    QString m_usbInterface;
    QString m_usbIpAddress;
    QString m_username;
    QString m_packageId;
    bool m_developerModeEnabled;
    DeveloperModeSettings::Status m_workStatus;
    int m_workProgress;
    PackageKit::Transaction::Role m_transactionRole;
    PackageKit::Transaction::Status m_transactionStatus;
    bool m_refreshedForInstall;
    bool m_localInstallFailed;
    QString m_localDeveloperModePackagePath;
    bool m_debugHomeEnabled;
    DeveloperModeSettings::InstallationType m_installationType;
};

void DeveloperModeSettingsPrivate::reportTransactionErrorCode(PackageKit::Transaction::Error code, const QString &details)
{
    qCWarning(lcDeveloperModeLog) << "Transaction error:" << code << details;
}

void DeveloperModeSettingsPrivate::updateState(int percentage, PackageKit::Transaction::Status status,
                                               PackageKit::Transaction::Role role)
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
                    workStatus = DeveloperModeSettings::DownloadingPackages;
                }
                rangeStart = 20;
                rangeEnd = 60;
                break;
            case PackageKit::Transaction::StatusInstall: // 60-100 %
                workStatus = DeveloperModeSettings::InstallingPackages;
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
                workStatus = DeveloperModeSettings::RemovingPackages;
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
        emit q->workProgressChanged();
    }
}

void DeveloperModeSettingsPrivate::resetState()
{
    if (m_installationType == DeveloperModeSettings::DeveloperMode) {
        bool enabled = QFile::exists(DEVELOPER_MODE_PROVIDED_FILE) || QFile::exists(EMULATOR_PROVIDED_FILE);
        if (m_developerModeEnabled != enabled) {
            m_developerModeEnabled = enabled;
            emit q->developerModeEnabledChanged();
        }
    } else if (m_installationType == DeveloperModeSettings::DebugHome) {
        if (m_debugHomeEnabled != debugHomeFolderExists()) {
            m_debugHomeEnabled = debugHomeFolderExists();
            emit q->debugHomeEnabledChanged();
        }
    }

    setWorkStatus(DeveloperModeSettings::Idle);
    setInstallationType(DeveloperModeSettings::None);

    if (m_workProgress != PROGRESS_INDETERMINATE) {
        m_workProgress = PROGRESS_INDETERMINATE;
        emit q->workProgressChanged();
    }
}

void DeveloperModeSettingsPrivate::setWorkStatus(DeveloperModeSettings::Status status)
{
    if (m_workStatus != status) {
        m_workStatus = status;
        emit q->workStatusChanged();
    }
}

void DeveloperModeSettingsPrivate::refreshPackageCacheAndInstall()
{
    m_refreshedForInstall = true;

    // Soft refresh, do not clear & reload valid cache.
    PackageKit::Transaction *refreshCache = PackageKit::Daemon::refreshCache(false);
    connect(refreshCache, &PackageKit::Transaction::errorCode,
            this, &DeveloperModeSettingsPrivate::reportTransactionErrorCode);
    connect(refreshCache, &PackageKit::Transaction::finished,
            this, [this](PackageKit::Transaction::Exit status, uint runtime) {
        qCDebug(lcDeveloperModeLog) << "Package cache updated:" << status << runtime;
        resolveAndExecute(InstallCommand); // trying again regardless of success, some repositories might be updated
    });
}

void DeveloperModeSettingsPrivate::resolveAndExecute(Command command)
{
    setWorkStatus(DeveloperModeSettings::Preparing);
    m_workProgress = 0;
    m_packageId.clear(); // might differ between installed/available

    if (command == InstallCommand
            && !m_localInstallFailed
            && !m_localDeveloperModePackagePath.isEmpty()
            && m_installationType == DeveloperModeSettings::DeveloperMode) {
        // Resolve which version of developer mode package is expected
        PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(DEVELOPER_MODE_PACKAGE"-preload",
                                                                              PackageKit::Transaction::FilterInstalled);
        connect(resolvePackage, &PackageKit::Transaction::errorCode,
                this, &DeveloperModeSettingsPrivate::reportTransactionErrorCode);
        connect(resolvePackage, &PackageKit::Transaction::package,
                this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
            Q_UNUSED(summary)
            Q_ASSERT(info == PackageKit::Transaction::InfoInstalled);
            const QString version = PackageKit::Transaction::packageVersion(packageID);
            m_localDeveloperModePackagePath = get_cached_package(version);
            emit q->repositoryAccessRequiredChanged();
            qCDebug(lcDeveloperModeLog) << "Preload package version: " << version
                                        << ", local package path: " << m_localDeveloperModePackagePath;
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
                        qCDebug(lcDeveloperModeLog)
                                << "Developer mode installation from local package transaction done:"
                                << status << runtime;
                        resetState();
                    } else if (status == PackageKit::Transaction::ExitFailed) {
                        qCWarning(lcDeveloperModeLog) << "Developer mode installation from local package failed, trying from repos";
                        m_localInstallFailed = true;
                        emit q->repositoryAccessRequiredChanged();
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

bool DeveloperModeSettingsPrivate::installAndRemove(Command command)
{
    if (q->packageName().isEmpty()) {
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

    PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(q->packageName(), filters);

    connect(resolvePackage, &PackageKit::Transaction::errorCode,
            this, &DeveloperModeSettingsPrivate::reportTransactionErrorCode);
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

void DeveloperModeSettingsPrivate::connectCommandSignals(PackageKit::Transaction *transaction)
{
    connect(transaction, &PackageKit::Transaction::errorCode,
            this, &DeveloperModeSettingsPrivate::reportTransactionErrorCode);
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

void DeveloperModeSettingsPrivate::setInstallationType(DeveloperModeSettings::InstallationType type)
{
    if (m_installationType != type) {
        m_installationType = type;
        emit q->installationTypeChanged();
    }
}

QString DeveloperModeSettingsPrivate::usbModedGetConfig(const QString &key, const QString &fallback)
{
    QString value = fallback;

    QDBusMessage msg = m_usbModeDaemon.blockingCall(USB_MODED_GET_NET_CONFIG, key);
    QList<QVariant> result = msg.arguments();
    if (result[0].toString() == key && result.size() == 2) {
        value = result[1].toString();
    }

    return value;
}

void DeveloperModeSettingsPrivate::usbModedSetConfig(const QString &key, const QString &value)
{
    m_usbModeDaemon.call(USB_MODED_SET_NET_CONFIG, key, value);
}

DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , d_ptr(new DeveloperModeSettingsPrivate(this))
{
    // Resolve and update local package path
    if (!d_ptr->m_localDeveloperModePackagePath.isEmpty()) {
        PackageKit::Transaction *resolvePackage = PackageKit::Daemon::resolve(DEVELOPER_MODE_PACKAGE"-preload",
                                                                              PackageKit::Transaction::FilterInstalled);
        connect(resolvePackage, &PackageKit::Transaction::errorCode,
                d_ptr, &DeveloperModeSettingsPrivate::reportTransactionErrorCode);
        connect(resolvePackage, &PackageKit::Transaction::package,
                this, [this](PackageKit::Transaction::Info info, const QString &packageID, const QString &summary) {
            Q_UNUSED(summary)
            Q_ASSERT(info == PackageKit::Transaction::InfoInstalled);
            const QString version = PackageKit::Transaction::packageVersion(packageID);
            d_ptr->m_localDeveloperModePackagePath = get_cached_package(version);

            if (d_ptr->m_localDeveloperModePackagePath.isEmpty()) {
                emit repositoryAccessRequiredChanged();
            }

            qCDebug(lcDeveloperModeLog) << "Preload package version: " << version << ", local package path: "
                                        << d_ptr->m_localDeveloperModePackagePath;
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
    return d_ptr->m_wlanIpAddress;
}

QString DeveloperModeSettings::usbIpAddress() const
{
    return d_ptr->m_usbIpAddress;
}

QString DeveloperModeSettings::username() const
{
    return d_ptr->m_username;
}

bool DeveloperModeSettings::developerModeEnabled() const
{
    return d_ptr->m_developerModeEnabled;
}

enum DeveloperModeSettings::Status DeveloperModeSettings::workStatus() const
{
    return d_ptr->m_workStatus;
}

int DeveloperModeSettings::workProgress() const
{
    return d_ptr->m_workProgress;
}

bool DeveloperModeSettings::repositoryAccessRequired() const
{
    // Aka local-install-of-developer-mode-package-is-not-possible
    return d_ptr->m_localInstallFailed || d_ptr->m_localDeveloperModePackagePath.isEmpty();
}

bool DeveloperModeSettings::debugHomeEnabled() const
{
    return d_ptr->m_debugHomeEnabled;
}

enum DeveloperModeSettings::InstallationType DeveloperModeSettings::installationType() const
{
    return d_ptr->m_installationType;
}

QString DeveloperModeSettings::packageName()
{
    if (d_ptr->m_installationType == DeveloperMode) {
        return DEVELOPER_MODE_PACKAGE;
    } else if (d_ptr->m_installationType == DebugHome) {
        return DEBUG_HOME_PACKAGE;
    } else {
        return QString();
    }
}

void DeveloperModeSettings::setDeveloperMode(bool enabled)
{
    if (d_ptr->m_developerModeEnabled != enabled) {
        if (d_ptr->m_workStatus != Idle) {
            qCWarning(lcDeveloperModeLog) << "DeveloperMode state change requested during activity, ignored.";
            return;
        }

        d_ptr->m_refreshedForInstall = false;
        d_ptr->setInstallationType(DeveloperMode);
        if (enabled) {
            d_ptr->resolveAndExecute(DeveloperModeSettingsPrivate::InstallCommand);
        } else {
            d_ptr->resolveAndExecute(DeveloperModeSettingsPrivate::RemoveCommand);
        }
    }
}

void DeveloperModeSettings::moveDebugToHome(bool enabled)
{
    if (d_ptr->m_debugHomeEnabled != enabled) {
        if (d_ptr->m_workStatus != Idle) {
            qCWarning(lcDeveloperModeLog) << "Debug home state change requested during activity, ignored.";
            return;
        }

        d_ptr->m_refreshedForInstall = false;
        d_ptr->setInstallationType(DebugHome);
        if (enabled) {
            d_ptr->resolveAndExecute(DeveloperModeSettingsPrivate::InstallCommand);
        } else {
            d_ptr->resolveAndExecute(DeveloperModeSettingsPrivate::RemoveCommand);
        }
    }
}

void DeveloperModeSettings::setUsbIpAddress(const QString &usbIpAddress)
{
    if (d_ptr->m_usbIpAddress != usbIpAddress) {
        d_ptr->usbModedSetConfig(USB_MODED_CONFIG_IP, usbIpAddress);
        d_ptr->m_usbIpAddress = usbIpAddress;
        emit usbIpAddressChanged();
    }
}

void DeveloperModeSettings::refresh()
{
    /* Retrieve network configuration from usb_moded */
    d_ptr->m_usbInterface = d_ptr->usbModedGetConfig(USB_MODED_CONFIG_INTERFACE, USB_NETWORK_FALLBACK_INTERFACE);
    QString usbIp = d_ptr->usbModedGetConfig(USB_MODED_CONFIG_IP, USB_NETWORK_FALLBACK_IP);

    if (usbIp != d_ptr->m_usbIpAddress) {
        d_ptr->m_usbIpAddress = usbIp;
        emit usbIpAddressChanged();
    }

    /* Retrieve network configuration from interfaces */
    QMap<QString, QString> entries = enumerate_network_interfaces();

    if (entries.contains(d_ptr->m_usbInterface)) {
        QString ip = entries[d_ptr->m_usbInterface];
        if (d_ptr->m_usbIpAddress != ip) {
            d_ptr->m_usbIpAddress = ip;
            emit usbIpAddressChanged();
        }
    }

    if (entries.contains(WLAN_NETWORK_INTERFACE)) {
        QString ip = entries[WLAN_NETWORK_INTERFACE];
        if (d_ptr->m_wlanIpAddress != ip) {
            d_ptr->m_wlanIpAddress = ip;
            emit wlanIpAddressChanged();
        }
    } else if (entries.contains(WLAN_NETWORK_FALLBACK_INTERFACE)) {
        // If the WLAN network interface does not have an IP address,
        // but there is a "tether" interface that does have an IP, assume
        // it is the WLAN interface in tethering mode, and use its IP.
        QString ip = entries[WLAN_NETWORK_FALLBACK_INTERFACE];
        if (d_ptr->m_wlanIpAddress != ip) {
            d_ptr->m_wlanIpAddress = ip;
            emit wlanIpAddressChanged();
        }
    }

    for (const QString &device : entries.keys()) {
        qCDebug(lcDeveloperModeLog) << "Device:" << device << "IP:" << entries[device];
    }
}

#include "developermodesettings.moc"
