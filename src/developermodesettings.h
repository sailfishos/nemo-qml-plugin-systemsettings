/*
 * Copyright (c) 2013 – 2019 Jolla Ltd.
 * Copyright (c) 2019 – 2020 Open Mobile Platform LLC.
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
    Q_ENUMS(InstallationType)

    Q_PROPERTY(QString wlanIpAddress READ wlanIpAddress NOTIFY wlanIpAddressChanged)
    Q_PROPERTY(QString usbIpAddress READ usbIpAddress NOTIFY usbIpAddressChanged)
    Q_PROPERTY(QString username READ username CONSTANT)
    Q_PROPERTY(bool developerModeEnabled READ developerModeEnabled NOTIFY developerModeEnabledChanged)
    Q_PROPERTY(enum DeveloperModeSettings::Status workStatus READ workStatus NOTIFY workStatusChanged)
    Q_PROPERTY(int workProgress READ workProgress NOTIFY workProgressChanged)
    Q_PROPERTY(bool repositoryAccessRequired READ repositoryAccessRequired NOTIFY repositoryAccessRequiredChanged)
    Q_PROPERTY(bool debugHomeEnabled READ debugHomeEnabled NOTIFY debugHomeEnabledChanged)
    Q_PROPERTY(enum DeveloperModeSettings::InstallationType installationType READ installationType NOTIFY installationTypeChanged)

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
    enum InstallationType {
        None,
        DeveloperMode,
        DebugHome
    };

    QString wlanIpAddress() const;
    QString usbIpAddress() const;
    QString username() const;
    bool developerModeEnabled() const;
    enum DeveloperModeSettings::Status workStatus() const;
    int workProgress() const;
    bool repositoryAccessRequired() const;
    bool debugHomeEnabled() const;
    QString packageName();
    enum DeveloperModeSettings::InstallationType installationType() const;

    Q_INVOKABLE void setDeveloperMode(bool enabled);
    Q_INVOKABLE void setUsbIpAddress(const QString &usbIpAddress);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void moveDebugToHome(bool enabled);

signals:
    void wlanIpAddressChanged();
    void usbIpAddressChanged();
    void developerModeEnabledChanged();
    void workStatusChanged();
    void workProgressChanged();
    void repositoryAccessRequiredChanged();
    void debugHomeEnabledChanged();
    void installationTypeChanged();

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
    bool installAndRemove(Command command);
    void connectCommandSignals(PackageKit::Transaction *transaction);
    void setInstallationType(InstallationType type);

    QString usbModedGetConfig(const QString &key, const QString &fallback);
    void usbModedSetConfig(const QString &key, const QString &value);

    QDBusInterface m_usbModeDaemon;
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

Q_DECLARE_METATYPE(DeveloperModeSettings::Status)
Q_DECLARE_METATYPE(DeveloperModeSettings::InstallationType)

#endif /* DEVELOPERMODESETTINGS_H */
