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

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDBusReply>
#include <getdef.h>
#include <pwd.h>

/* Symbolic constants */
#define PROGRESS_INDETERMINATE (-1)

/* Interfaces for IP addresses */
#define USB_NETWORK_FALLBACK_INTERFACE "usb0"
#define USB_NETWORK_FALLBACK_IP "192.168.2.15"
#define WLAN_NETWORK_INTERFACE "wlan0"
#define WLAN_NETWORK_FALLBACK_INTERFACE "tether"

/* Additional developer mode tools package */
#define SDK_TOOLS_PACKAGE "jolla-developer-mode-sdk-tools"

/* A file that is provided by the developer mode package */
#define SDK_TOOLS_PROVIDED_FILE "/usr/share/jolla-developer-mode/.sdk-tools-installed"

/* D-Bus service */
#define STORE_CLIENT_SERVICE "com.jolla.jollastore"
#define STORE_CLIENT_PATH "/StoreClient"
#define STORE_CLIENT_INTERFACE "com.jolla.jollastore"

/* D-Bus method names */
#define STORE_CLIENT_INSTALL_PACKAGE "installPackage"
#define STORE_CLIENT_REMOVE_PACKAGE "removePackage"
#define STORE_CLIENT_REPORT_DEVELOPER_MODE "reportDeveloperMode"

/* D-Bus signal names */
#define STORE_CLIENT_INSTALL_PACKAGE_RESULT "installPackageResult"
#define STORE_CLIENT_REMOVE_PACKAGE_RESULT "removePackageResult"
#define STORE_CLIENT_PACKAGE_PROGRESS_CHANGED "packageProgressChanged"

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

// Uncomment following line to enable debugs
// #define ENABLE_DEBUGS
#ifdef ENABLE_DEBUGS
# define DEBUG qDebug
#else
# define DEBUG if (0) qDebug
#endif

SdkToolsInstallWorker::SdkToolsInstallWorker(QObject *parent)
    : QObject(parent)
    , m_working(false)
    , m_sessionBus(QDBusConnection::sessionBus())
    , m_storeClient(STORE_CLIENT_SERVICE, STORE_CLIENT_PATH,
            STORE_CLIENT_INTERFACE)
{
    m_sessionBus.connect("", "", STORE_CLIENT_INTERFACE,
            STORE_CLIENT_INSTALL_PACKAGE_RESULT,
            this, SLOT(onInstallPackageResult(QString, bool)));
    m_sessionBus.connect("", "", STORE_CLIENT_INTERFACE,
            STORE_CLIENT_REMOVE_PACKAGE_RESULT,
            this, SLOT(onRemovePackageResult(QString, bool)));
    m_sessionBus.connect("", "", STORE_CLIENT_INTERFACE,
            STORE_CLIENT_PACKAGE_PROGRESS_CHANGED,
            this, SLOT(onPackageProgressChanged(QString, int)));
}

void SdkToolsInstallWorker::retrieveSdkToolsStatus()
{
    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring retrieveSdkToolsStatus request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::CheckingStatus);
    bool installed = QFile(SDK_TOOLS_PROVIDED_FILE).exists();
    emit statusChanged(false, DeveloperModeSettings::Idle);
    emit sdkToolsInstalledChanged(installed);
    m_working = false;
}

void SdkToolsInstallWorker::installSdkTools()
{
    DEBUG() << Q_FUNC_INFO << "working" << m_working;

    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring installSdkTools request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::Installing);
    m_storeClient.call(STORE_CLIENT_INSTALL_PACKAGE, SDK_TOOLS_PACKAGE);
    QDBusError error = m_storeClient.lastError();
    if (error.isValid()) {
        qWarning() << "Could not enable developer mode: " << error.message();
        emit statusChanged(false, DeveloperModeSettings::Failure);
        m_working = false;
    }
}

void SdkToolsInstallWorker::removeSdkTools()
{
    DEBUG() << Q_FUNC_INFO << "working" << m_working;
    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring removeSdkTools request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::Removing);
    m_storeClient.call(STORE_CLIENT_REMOVE_PACKAGE, SDK_TOOLS_PACKAGE, true);
    QDBusError error = m_storeClient.lastError();
    if (error.isValid()) {
        qWarning() << "Could not disable developer mode: " << error.message();
        emit statusChanged(false, DeveloperModeSettings::Failure);
        m_working = false;
    }
}

void SdkToolsInstallWorker::onInstallPackageResult(QString packageName, bool success)
{
    qDebug() << "onInstallPackageResult:" << packageName << success;
    if (packageName == SDK_TOOLS_PACKAGE) {
        emit statusChanged(false, success ? DeveloperModeSettings::Success :
                DeveloperModeSettings::Failure);
        emit sdkToolsInstalledChanged(success);
        m_working = false;
    }
}

void SdkToolsInstallWorker::onRemovePackageResult(QString packageName, bool success)
{
    qDebug() << "onRemovePackageResult:" << packageName << success;
    if (packageName == SDK_TOOLS_PACKAGE) {
        emit statusChanged(false, success ? DeveloperModeSettings::Success :
                DeveloperModeSettings::Failure);
        emit sdkToolsInstalledChanged(!success);
        m_working = false;
    }
}

void SdkToolsInstallWorker::onPackageProgressChanged(QString packageName, int progress)
{
    qDebug() << "onPackageProgressChanged:" << packageName << progress;
    if (packageName == SDK_TOOLS_PACKAGE) {
        if (m_working) {
            emit progressChanged(progress);
        }
    }
}

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

static inline QString usb_moded_get_config(QDBusInterface &usb, QString key, QString fallback)
{
    QString value = fallback;

    QDBusMessage msg = usb.call(USB_MODED_GET_NET_CONFIG, key);
    QList<QVariant> result = msg.arguments();
    if (result[0].toString() == key && result.size() == 2) {
        value = result[1].toString();
    }

    return value;
}

static inline void usb_moded_set_config(QDBusInterface &usb, QString key, QString value)
{
    usb.call(USB_MODED_SET_NET_CONFIG, key, value);
}

DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , m_worker_thread()
    , m_worker(new SdkToolsInstallWorker)
    , m_usbModeDaemon(USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE,
            QDBusConnection::systemBus())
    , m_wlanIpAddress("-")
    , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
    , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
    , m_username("nemo")
    , m_workerWorking(false)
    , m_workerStatus(Idle)
    , m_workerProgress(PROGRESS_INDETERMINATE)
    , m_sdkToolsInstalled(false)
    , m_developerModeEnabled(QLatin1String("/sailfish/developermode/enabled"))

{
    int uid = getdef_num("UID_MIN", -1);
    struct passwd *pwd;
    if ((pwd = getpwuid(uid)) != NULL) {
        m_username = QString(pwd->pw_name);
    } else {
        qWarning() << "Failed to return username using getpwuid()";
    }

    m_worker->moveToThread(&m_worker_thread);

    /* Messages to worker */
    QObject::connect(this, SIGNAL(workerRetrieveSdkToolsStatus()),
            m_worker, SLOT(retrieveSdkToolsStatus()));
    QObject::connect(this, SIGNAL(workerInstallSdkTools()),
            m_worker, SLOT(installSdkTools()));
    QObject::connect(this, SIGNAL(workerRemoveSdkTools()),
            m_worker, SLOT(removeSdkTools()));

    connect(&m_developerModeEnabled, &MGConfItem::valueChanged, this, &DeveloperModeSettings::developerModeEnabledChanged);
    connect(this, &DeveloperModeSettings::developerModeEnabledChanged, this, &DeveloperModeSettings::onDeveloperModeEnabled);

    /* Messages from worker */
    QObject::connect(m_worker, SIGNAL(statusChanged(bool, enum DeveloperModeSettings::Status)),
            this, SLOT(onWorkerStatusChanged(bool, enum DeveloperModeSettings::Status)));
    QObject::connect(m_worker, SIGNAL(sdkToolsInstalledChanged(bool)),
            this, SLOT(onWorkerSdkToolsInstalledChanged(bool)));
    QObject::connect(m_worker, SIGNAL(progressChanged(int)),
            this, SLOT(onWorkerProgressChanged(int)));

    m_worker_thread.start();

    refresh();

    // Get current SDK tools install status
    emit workerRetrieveSdkToolsStatus();

    // TODO: Watch WLAN / USB IP addresses for changes
    // TODO: Watch package manager for changes to developer mode
}

DeveloperModeSettings::~DeveloperModeSettings()
{
    m_worker_thread.quit();
    m_worker_thread.wait();

    delete m_worker;
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
    return m_developerModeEnabled.value(false).toBool();
}

bool DeveloperModeSettings::sdkToolsInstalled() const
{
    return m_sdkToolsInstalled;
}

bool DeveloperModeSettings::workerWorking() const
{
    return m_workerWorking;
}

enum DeveloperModeSettings::Status DeveloperModeSettings::workerStatus() const
{
    return m_workerStatus;
}

int DeveloperModeSettings::workerProgress() const
{
    return m_workerProgress;
}

void DeveloperModeSettings::setDeveloperModeEnabled(bool enabled)
{
    bool oldEnabled = developerModeEnabled();
    DEBUG() << Q_FUNC_INFO << "from" << oldEnabled << "to" << enabled;
    if (oldEnabled != enabled) {
        m_developerModeEnabled.set(enabled);
    }
}

void DeveloperModeSettings::onDeveloperModeEnabled()
{
    DEBUG() << Q_FUNC_INFO << "enabled" << developerModeEnabled();

    if (developerModeEnabled()) {
        QDBusInterface iface(STORE_CLIENT_SERVICE,
                             STORE_CLIENT_PATH,
                             STORE_CLIENT_INTERFACE);
        iface.call(STORE_CLIENT_REPORT_DEVELOPER_MODE);
    }
}

void DeveloperModeSettings::installSdkTools()
{
    DEBUG() << Q_FUNC_INFO;

    if (!m_sdkToolsInstalled) {
        emit workerInstallSdkTools();
    }
}

void DeveloperModeSettings::removeSdkTools()
{
    DEBUG() << Q_FUNC_INFO;
    if (m_sdkToolsInstalled) {
        emit workerRemoveSdkTools();
    }
}

void DeveloperModeSettings::setUsbIpAddress(const QString &usbIpAddress)
{
    if (m_usbIpAddress != usbIpAddress) {
        usb_moded_set_config(m_usbModeDaemon, USB_MODED_CONFIG_IP, usbIpAddress);
        m_usbIpAddress = usbIpAddress;
        emit usbIpAddressChanged();
    }
}

void DeveloperModeSettings::refresh()
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
        DEBUG() << "Device:" << device
                 << "IP:" << ip;
    }
}

void DeveloperModeSettings::onWorkerStatusChanged(bool working, enum DeveloperModeSettings::Status status)
{
    if (m_workerWorking != working) {
        m_workerWorking = working;
        emit workerWorkingChanged();
    }

    if (m_workerStatus != status) {
        m_workerStatus = status;
        emit workerStatusChanged();
    }
}

void DeveloperModeSettings::onWorkerProgressChanged(int progress)
{
    if (m_workerProgress != progress) {
        m_workerProgress = progress;
        emit workerProgressChanged();
    }
}

void DeveloperModeSettings::onWorkerSdkToolsInstalledChanged(bool installed)
{
    if (m_sdkToolsInstalled != installed) {
        m_sdkToolsInstalled = installed;
        emit sdkToolsInstalledChanged();
    }
}
