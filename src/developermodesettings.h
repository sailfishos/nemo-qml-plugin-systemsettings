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

    Q_PROPERTY(QString wlanIpAddress
            READ wlanIpAddress
            NOTIFY wlanIpAddressChanged)

    Q_PROPERTY(QString usbIpAddress
            READ usbIpAddress
            NOTIFY usbIpAddressChanged)

    Q_PROPERTY(QString username
               READ username
               CONSTANT)

    Q_PROPERTY(bool developerModeAvailable
            READ developerModeAvailable
            NOTIFY developerModeAvailableChanged)

    Q_PROPERTY(bool developerModeEnabled
            READ developerModeEnabled
            NOTIFY developerModeEnabledChanged)

    Q_PROPERTY(bool remoteLoginEnabled
            READ remoteLoginEnabled
            NOTIFY remoteLoginEnabledChanged)

    Q_PROPERTY(enum DeveloperModeSettings::Status workerStatus
            READ workerStatus
            NOTIFY workerStatusChanged)

    Q_PROPERTY(int workerProgress
            READ workerProgress
            NOTIFY workerProgressChanged)

public:
    explicit DeveloperModeSettings(QObject *parent = NULL);
    virtual ~DeveloperModeSettings();

    enum Status {
        Idle = 0,
        InitialCheckingStatus,
        CheckingStatus,
        Preparing,
        DownloadingPackages,
        InstallingPackages,
        RemovingPackages
    };

    QString wlanIpAddress() const;
    QString usbIpAddress() const;
    QString username() const;
    bool developerModeAvailable() const;
    bool developerModeEnabled() const;
    bool remoteLoginEnabled() const;
    enum DeveloperModeSettings::Status workerStatus() const;
    int workerProgress() const;

    Q_INVOKABLE void setDeveloperMode(bool enabled);
    Q_INVOKABLE void setRemoteLogin(bool enabled);
    Q_INVOKABLE void setUsbIpAddress(const QString &usbIpAddress);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void checkDeveloperModeStatus();

signals:
    void wlanIpAddressChanged();
    void usbIpAddressChanged();
    void developerModeAvailableChanged();
    void developerModeEnabledChanged();
    void remoteLoginEnabledChanged();
    void workerWorkingChanged();
    void workerStatusChanged();
    void workerProgressChanged();
    void packageCacheUpdated();

private slots:
    void transactionPackage(PackageKit::Transaction::Info info, const QString &packageId, const QString &summary);
    void transactionErrorCode(PackageKit::Transaction::Error code, const QString &details);
    void transactionFinished(PackageKit::Transaction::Exit status, uint runtime);
    void updateState(int percentage, PackageKit::Transaction::Status status, PackageKit::Transaction::Role role);

private:
    enum Command {
        NoCommand,
        InstallCommand,
        RemoveCommand
    };

    void refreshPackageCache();
    void resolveDeveloperModePackageId(Command command);
    void connectCommandSignals(PackageKit::Transaction *transaction);
    void checkDeveloperModeStatus(bool initial);

    QDBusInterface m_usbModeDaemon;
    QString m_wlanIpAddress;
    QString m_usbInterface;
    QString m_usbIpAddress;
    QString m_username;
    QString m_developerModePackageId;
    bool m_developerModeEnabled;
    bool m_remoteLoginEnabled;
    DeveloperModeSettings::Status m_workerStatus;
    int m_workerProgress;
    PackageKit::Transaction::Role m_transactionRole;
    PackageKit::Transaction::Status m_transactionStatus;
    bool m_cacheUpdated;
};

Q_DECLARE_METATYPE(DeveloperModeSettings::Status);

#endif /* DEVELOPERMODESETTINGS_H */
