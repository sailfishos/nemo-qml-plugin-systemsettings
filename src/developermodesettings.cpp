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

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


/* Interfaces for IP addresses */
#define USB_NETWORK_INTERFACE "rndis0"
#define WLAN_NETWORK_INTERFACE "wlan0"

/* Developer mode package */
#define DEVELOPER_MODE_PACKAGE "jolla-developer-mode"

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
}

void
DeveloperModeSettingsWorker::retrieveDeveloperModeStatus()
{
    if (m_working) {
        // Ignore request - something else in progress
        return;
    }

    m_working = true;
    emit statusChanged(true, "Retrieving status" /* XXX: i18n */);

    QDBusReply<bool> enabled = m_storeClient.call(STORE_CLIENT_CHECK_INSTALLED,
            DEVELOPER_MODE_PACKAGE);

    emit statusChanged(false, "");
    emit developerModeEnabledChanged(enabled.value());
    m_working = false;
}

void
DeveloperModeSettingsWorker::enableDeveloperMode()
{
    if (m_working) {
        // Ignore request - something else in progress
        return;
    }

    m_working = true;
    emit statusChanged(true, "Enabling developer mode" /* XXX: i18n */);
    m_storeClient.call(STORE_CLIENT_INSTALL_PACKAGE, DEVELOPER_MODE_PACKAGE);
}

void
DeveloperModeSettingsWorker::disableDeveloperMode()
{
    if (m_working) {
        // Ignore request - something else in progress
        return;
    }

    m_working = true;
    emit statusChanged(true, "Disabling developer mode" /* XXX: i18n */);
    m_storeClient.call(STORE_CLIENT_REMOVE_PACKAGE, DEVELOPER_MODE_PACKAGE, true);
}

void
DeveloperModeSettingsWorker::onInstallPackageResult(QString packageName, bool success)
{
    qDebug() << "onInstallPackageResult:" << packageName << success;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        emit statusChanged(false, "");
        emit developerModeEnabledChanged(success);
        m_working = false;
    }
}

void
DeveloperModeSettingsWorker::onRemovePackageResult(QString packageName, bool success)
{
    qDebug() << "onRemovePackageResult:" << packageName << success;
    if (packageName == DEVELOPER_MODE_PACKAGE) {
        emit statusChanged(false, "");
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
            emit statusChanged(true, QString("Progress: %1").arg(progress));
        }
    }
}


NetworkAddressEnumerator::NetworkAddressEnumerator()
{
}

NetworkAddressEnumerator::~NetworkAddressEnumerator()
{
}

QMap<QString,NetworkAddressEntry>
NetworkAddressEnumerator::enumerate()
{
    QMap<QString,NetworkAddressEntry> result;

    QDir dir("/sys/class/net");
    foreach (QString device, dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString filename = QString("/sys/class/net/%1/operstate").arg(device);

        QFile operstate(filename);
        if (!operstate.open(QIODevice::ReadOnly)) {
            qDebug() << "Cannot open: " << filename;
            continue;
        }

        QByteArray data = operstate.readAll();
        QString state = QString::fromUtf8(data).trimmed();
        operstate.close();

        result[device] = NetworkAddressEntry(state, getIP(device));
    }

    return result;
}

QString
NetworkAddressEnumerator::getIP(QString device)
{
    QByteArray device_utf8(device.toUtf8());
    QString result = "-";

    struct ifreq ifr;
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;

    memset(&ifr, 0, sizeof(ifr));
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        qDebug() << "Cannot open socket in getIP()";
        return result;
    }

    strcpy(ifr.ifr_name, device_utf8.data());
    sin->sin_family = AF_INET;

    if (ioctl(sfd, SIOCGIFADDR, &ifr) != 0) {
        close(sfd);
        //qDebug() << "ioctl(SIOCGIFRADDR)";
        return result;
    }

    result = QString::fromUtf8(inet_ntoa(sin->sin_addr));
    close(sfd);

    return result;
}


DeveloperModeSettings::DeveloperModeSettings(QObject *parent)
    : QObject(parent)
    , m_worker_thread()
    , m_worker(new DeveloperModeSettingsWorker)
    , m_enumerator()
    , m_wlanIpAddress("-")
    , m_usbIpAddress("-")
    , m_developerModeEnabled(false)
    , m_remoteLoginEnabled(false) // TODO: Read (from password manager?)
    , m_workerWorking(false)
    , m_workerMessage("")
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
    QObject::connect(m_worker, SIGNAL(statusChanged(bool, QString)),
            this, SLOT(onWorkerStatusChanged(bool, QString)));
    QObject::connect(m_worker, SIGNAL(developerModeEnabledChanged(bool)),
            this, SLOT(onWorkerDeveloperModeEnabledChanged(bool)));

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

const QString
DeveloperModeSettings::wlanIpAddress() const
{
    return m_wlanIpAddress;
}

const QString
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

const QString
DeveloperModeSettings::workerMessage() const
{
    return m_workerMessage;
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
    // TODO: Really set this (maybe through the worker?)
    m_usbIpAddress = usbIpAddress;
    emit usbIpAddressChanged();
}

void
DeveloperModeSettings::refresh()
{
    QMap<QString,NetworkAddressEntry> entries = m_enumerator.enumerate();

    if (entries.contains(USB_NETWORK_INTERFACE)) {
        NetworkAddressEntry entry = entries[USB_NETWORK_INTERFACE];
        qDebug() << "Got USB IP, state =" << entry.state;
        if (m_usbIpAddress != entry.ip) {
            m_usbIpAddress = entry.ip;
            emit usbIpAddressChanged();
        }
    }

    if (entries.contains(WLAN_NETWORK_INTERFACE)) {
        NetworkAddressEntry entry = entries[WLAN_NETWORK_INTERFACE];
        qDebug() << "Got WLAN IP, state =" << entry.state;
        if (m_wlanIpAddress != entry.ip) {
            m_wlanIpAddress = entry.ip;
            emit wlanIpAddressChanged();
        }
    }

    foreach (QString device, entries.keys()) {
        NetworkAddressEntry entry = entries[device];
        qDebug() << "Device:" << device
                 << "IP:" << entry.ip
                 << "State:" << entry.state;
    }
}

void
DeveloperModeSettings::onWorkerStatusChanged(bool working, QString message)
{
    if (m_workerWorking != working) {
        m_workerWorking = working;
        emit workerWorkingChanged();
    }

    if (m_workerMessage != message) {
        m_workerMessage = message;
        emit workerMessageChanged();
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
