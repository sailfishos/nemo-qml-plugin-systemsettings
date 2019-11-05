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

#ifndef DEVELOPERMODESETTINGS_H
#define DEVELOPERMODESETTINGS_H

#include <QObject>
#include <QDBusInterface>

#include <systemsettingsglobal.h>
#include <daemon.h>


QT_BEGIN_NAMESPACE
class QDBusPendingCallWatcher;
QT_END_NAMESPACE

class SYSTEMSETTINGS_EXPORT DeveloperModeSettings : public QObject
{
    Q_OBJECT
    Q_ENUMS(Status)

    Q_PROPERTY(QString wlanIpAddress READ wlanIpAddress NOTIFY wlanIpAddressChanged)
    Q_PROPERTY(QString usbIpAddress READ usbIpAddress NOTIFY usbIpAddressChanged)
    Q_PROPERTY(QString username READ username CONSTANT)
    Q_PROPERTY(bool developerModeEnabled READ developerModeEnabled NOTIFY developerModeEnabledChanged)
    Q_PROPERTY(enum DeveloperModeSettings::Status workStatus READ workStatus NOTIFY workStatusChanged)
    Q_PROPERTY(int workProgress READ workProgress NOTIFY workProgressChanged)
    Q_PROPERTY(bool repositoryAccessRequired READ repositoryAccessRequired NOTIFY repositoryAccessRequiredChanged)

public:
    explicit DeveloperModeSettings(QObject *parent = NULL);
    virtual ~DeveloperModeSettings();

    enum Status {
        Idle = 0,
        Preparing,
        DownloadingPackages,
        InstallingPackages,
        RemovingPackages
    };

    QString wlanIpAddress() const;
    QString usbIpAddress() const;
    QString username() const;
    bool developerModeEnabled() const;
    enum DeveloperModeSettings::Status workStatus() const;
    int workProgress() const;
    bool repositoryAccessRequired() const;

    Q_INVOKABLE void setDeveloperMode(bool enabled);
    Q_INVOKABLE void setUsbIpAddress(const QString &usbIpAddress);
    Q_INVOKABLE void refresh();

signals:
    void wlanIpAddressChanged();
    void usbIpAddressChanged();
    void developerModeEnabledChanged();
    void workStatusChanged();
    void workProgressChanged();
    void repositoryAccessRequiredChanged();

private slots:
    void reportTransactionErrorCode(PackageKit::Transaction::Error code, const QString &details);
    void updateState(int percentage, PackageKit::Transaction::Status status, PackageKit::Transaction::Role role);

private:
    enum Command {
        InstallCommand,
        RemoveCommand
    };

    void resetState();
    void setWorkStatus(Status status);
    void refreshPackageCacheAndInstall();
    void resolveAndExecute(Command command);
    void connectCommandSignals(PackageKit::Transaction *transaction);

    QString usbModedGetConfig(const QString &key, const QString &fallback);
    void usbModedSetConfig(const QString &key, const QString &value);

    QDBusInterface m_usbModeDaemon;
    QString m_wlanIpAddress;
    QString m_usbInterface;
    QString m_usbIpAddress;
    QString m_username;
    QString m_developerModePackageId;
    bool m_developerModeEnabled;
    DeveloperModeSettings::Status m_workStatus;
    int m_workProgress;
    PackageKit::Transaction::Role m_transactionRole;
    PackageKit::Transaction::Status m_transactionStatus;
    bool m_refreshedForInstall;
    bool m_localInstallFailed;
    QString m_localDeveloperModePackagePath;
};

Q_DECLARE_METATYPE(DeveloperModeSettings::Status)

#endif /* DEVELOPERMODESETTINGS_H */
