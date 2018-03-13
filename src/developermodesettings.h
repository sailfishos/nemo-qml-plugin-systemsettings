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
#include <QDBusObjectPath>

#include <systemsettingsglobal.h>

QT_BEGIN_NAMESPACE
class QDBusPendingCallWatcher;
class AccountManager;
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

    Q_PROPERTY(QString developerModeAccountProvider
            READ developerModeAccountProvider
            NOTIFY developerModeAccountProviderChanged)

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
    QString developerModeAccountProvider() const;
    bool remoteLoginEnabled() const;
    enum DeveloperModeSettings::Status workerStatus() const;
    int workerProgress() const;

    Q_INVOKABLE void setDeveloperMode(bool enabled);
    Q_INVOKABLE void setRemoteLogin(bool enabled);
    Q_INVOKABLE void setUsbIpAddress(const QString &usbIpAddress);
    Q_INVOKABLE void refresh();

signals:
    void wlanIpAddressChanged();
    void usbIpAddressChanged();
    void developerModeAvailableChanged();
    void developerModeAccountProviderChanged();
    void developerModeEnabledChanged();
    void remoteLoginEnabledChanged();
    void workerWorkingChanged();
    void workerStatusChanged();
    void workerProgressChanged();

private slots:
    void transactionPackage(uint info, const QString &packageId);
    void transactionItemProgress(const QString &package, uint status, uint progress);
    void transactionErrorCode(uint code, const QString &message);
    void transactionFinished(uint exit, uint runtime);
    void transactionPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    QDBusPendingCallWatcher *resolvePackageId(const QString &packageName);
    QDBusPendingCallWatcher *installPackage(const QString &packageId);
    QDBusPendingCallWatcher *removePackage(const QString &packageId);

    void connectTransactionSignal(const QString &name, const char *slot);
    void connectPropertiesChanged();

    void executePackageKitCommand(
            QDBusPendingCallWatcher *(DeveloperModeSettings::*command)(const QString &),
            const QString &argument);
//
//    void updateAccountProvider();

    QDBusInterface m_usbModeDaemon;
    QDBusObjectPath m_packageKitTransaction;
    AccountManager *m_accountManager;
    QDBusPendingCallWatcher *m_pendingPackageKitCall;
    QDBusPendingCallWatcher *(DeveloperModeSettings::*m_packageKitCommand)(const QString &packageId);

    QList<int> m_statusChanges;
    QString m_wlanIpAddress;
    QString m_usbInterface;
    QString m_usbIpAddress;
    QString m_username;
    QString m_developerModePackageId;
    QString m_developerModeAccountProvider;
    bool m_developerModeEnabled;
    bool m_remoteLoginEnabled;
    DeveloperModeSettings::Status m_workerStatus;
    int m_workerProgress;
    int m_transactionRole;
    int m_transactionStatus;

};

Q_DECLARE_METATYPE(DeveloperModeSettings::Status);

#endif /* DEVELOPERMODESETTINGS_H */
