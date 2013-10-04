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

/* Symbolic constants */
#define PROGRESS_INDETERMINATE (-1)

/* Interfaces for IP addresses */
#define USB_NETWORK_FALLBACK_INTERFACE "usb0"
#define USB_NETWORK_FALLBACK_IP "192.168.2.15"
#define WLAN_NETWORK_INTERFACE "wlan0"

/* Developer mode package */
#define DEVELOPER_MODE_PACKAGE "jolla-developer-mode"

/* A file that is provided by the developer mode package */
#define DEVELOPER_MODE_PROVIDED_FILE "/usr/bin/devel-su"

/* D-Bus service */
#define STORE_CLIENT_SERVICE "com.jolla.jollastore"
#define STORE_CLIENT_PATH "/StoreClient"
#define STORE_CLIENT_INTERFACE "com.jolla.jollastore"

/* D-Bus method names */
#define STORE_CLIENT_CHECK_INSTALLED "checkInstalled"
#define STORE_CLIENT_INSTALL_PACKAGE "installPackage"
#define STORE_CLIENT_REMOVE_PACKAGE "removePackage"

/* D-Bus signal names */
#define STORE_CLIENT_INSTALL_PACKAGE_RESULT "installPackageResult"
#define STORE_CLIENT_REMOVE_PACKAGE_RESULT "removePackageResult"
#define STORE_CLIENT_PACKAGE_PROGRESS_CHANGED "packageProgressChanged"
#define STORE_CLIENT_CHECK_INSTALLED_RESULT "checkInstalledResult"

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


DeveloperModeSettingsWorker::DeveloperModeSettingsWorker(QObject *parent)
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
    m_sessionBus.connect("", "", STORE_CLIENT_INTERFACE,
            STORE_CLIENT_CHECK_INSTALLED_RESULT,
            this, SLOT(onCheckInstalledResult(QString, bool)));
}

void
DeveloperModeSettingsWorker::retrieveDeveloperModeStatus()
{
    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring retrieveDeveloperModeStatus request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::CheckingStatus);
    m_storeClient.call(STORE_CLIENT_CHECK_INSTALLED, DEVELOPER_MODE_PACKAGE);
    QDBusError error = m_storeClient.lastError();
    if (error.isValid()) {
        qWarning() << "Could not query developer mode status: " << error.message();
        emit statusChanged(false, DeveloperModeSettings::Idle);
        // Fallback to detecting developer mode by existence of a provided file
        emit developerModeEnabledChanged(QFile(DEVELOPER_MODE_PROVIDED_FILE).exists());
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::enableDeveloperMode()
{
    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring enableDeveloperMode request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::Installing);
    m_storeClient.call(STORE_CLIENT_INSTALL_PACKAGE, DEVELOPER_MODE_PACKAGE);
    QDBusError error = m_storeClient.lastError();
    if (error.isValid()) {
        qWarning() << "Could not enable developer mode: " << error.message();
        emit statusChanged(false, DeveloperModeSettings::Failure);
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::disableDeveloperMode()
{
    if (m_working) {
        // Ignore request - something else in progress
        qWarning() << "Ignoring disableDeveloperMode request (m_working is true)";
        return;
    }

    m_working = true;
    emit progressChanged(PROGRESS_INDETERMINATE);
    emit statusChanged(true, DeveloperModeSettings::Removing);
    m_storeClient.call(STORE_CLIENT_REMOVE_PACKAGE, DEVELOPER_MODE_PACKAGE, true);
    QDBusError error = m_storeClient.lastError();
    if (error.isValid()) {
        qWarning() << "Could not disable developer mode: " << error.message();
        emit statusChanged(false, DeveloperModeSettings::Failure);
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::onInstallPackageResult(QString packageName, bool success)
{
    qDebug() << "onInstallPackageResult:" << packageName << success;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        emit statusChanged(false, success ? DeveloperModeSettings::Success :
                DeveloperModeSettings::Failure);
        emit developerModeEnabledChanged(success);
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::onRemovePackageResult(QString packageName, bool success)
{
    qDebug() << "onRemovePackageResult:" << packageName << success;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        emit statusChanged(false, success ? DeveloperModeSettings::Success :
                DeveloperModeSettings::Failure);
        emit developerModeEnabledChanged(!success);
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::onPackageProgressChanged(QString packageName, int progress)
{
    qDebug() << "onPackageProgressChanged:" << packageName << progress;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        if (m_working) {
            emit progressChanged(progress);
        }
    }
}

void
DeveloperModeSettingsWorker::onCheckInstalledResult(QString packageName, bool installed)
{
    qDebug() << "onCheckInstalledResult:" << packageName << installed;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        emit statusChanged(false, DeveloperModeSettings::Idle);
        emit developerModeEnabledChanged(installed);
        m_working = false;
    }
}

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
    , m_worker_thread()
    , m_worker(new DeveloperModeSettingsWorker)
    , m_usbModeDaemon(USB_MODED_SERVICE, USB_MODED_PATH, USB_MODED_INTERFACE,
            QDBusConnection::systemBus())
    , m_wlanIpAddress("-")
    , m_usbInterface(USB_NETWORK_FALLBACK_INTERFACE)
    , m_usbIpAddress(USB_NETWORK_FALLBACK_IP)
    , m_developerModeEnabled(false)
    , m_remoteLoginEnabled(false) // TODO: Read (from password manager?)
    , m_workerWorking(false)
    , m_workerStatus(Idle)
    , m_workerProgress(PROGRESS_INDETERMINATE)
{
    m_worker->moveToThread(&m_worker_thread);

    /* Messages to worker */
    QObject::connect(this, SIGNAL(workerRetrieveDeveloperModeStatus()),
            m_worker, SLOT(retrieveDeveloperModeStatus()));
    QObject::connect(this, SIGNAL(workerEnableDeveloperMode()),
            m_worker, SLOT(enableDeveloperMode()));
    QObject::connect(this, SIGNAL(workerDisableDeveloperMode()),
            m_worker, SLOT(disableDeveloperMode()));

    /* Messages from worker */
    QObject::connect(m_worker, SIGNAL(statusChanged(bool, enum DeveloperModeSettings::Status)),
            this, SLOT(onWorkerStatusChanged(bool, enum DeveloperModeSettings::Status)));
    QObject::connect(m_worker, SIGNAL(developerModeEnabledChanged(bool)),
            this, SLOT(onWorkerDeveloperModeEnabledChanged(bool)));
    QObject::connect(m_worker, SIGNAL(progressChanged(int)),
            this, SLOT(onWorkerProgressChanged(int)));

    m_worker_thread.start();

    refresh();

    // Get current developer mode status
    emit workerRetrieveDeveloperModeStatus();

    // TODO: Watch WLAN / USB IP addresses for changes
    // TODO: Watch package manager for changes to developer mode
}

DeveloperModeSettings::~DeveloperModeSettings()
{
    m_worker_thread.quit();
    m_worker_thread.wait();

    delete m_worker;
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

bool
DeveloperModeSettings::workerWorking() const
{
    return m_workerWorking;
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
    if (m_developerModeEnabled != enabled) {
        if (enabled) {
            emit workerEnableDeveloperMode();
        } else {
            emit workerDisableDeveloperMode();
        }
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
        m_usbInterface = usbIp;
        emit usbIpAddressChanged();
    }

    /* Retrieve network configuration from interfaces */
    QMap<QString,QString> entries = enumerate_network_interfaces();

    if (entries.contains(m_usbInterface)) {
        QString ip = entries[m_usbIpAddress];
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
    }

    foreach (const QString &device, entries.keys()) {
        QString ip = entries[device];
        qDebug() << "Device:" << device
                 << "IP:" << ip;
    }
}

void
DeveloperModeSettings::onWorkerStatusChanged(bool working, enum DeveloperModeSettings::Status status)
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

void
DeveloperModeSettings::onWorkerProgressChanged(int progress)
{
    if (m_workerProgress != progress) {
        m_workerProgress = progress;
        emit workerProgressChanged();
    }
}

void
DeveloperModeSettings::onWorkerDeveloperModeEnabledChanged(bool enabled)
{
    if (m_developerModeEnabled != enabled) {
        m_developerModeEnabled = enabled;
        emit developerModeEnabledChanged();
    }
}
