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

class DeveloperModeSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString wlanIpAddress
            READ wlanIpAddress
            NOTIFY wlanIpAddressChanged)

    Q_PROPERTY(QString usbIpAddress
            READ usbIpAddress
            NOTIFY usbIpAddressChanged)

    Q_PROPERTY(bool developerModeEnabled
            READ developerModeEnabled
            NOTIFY developerModeEnabledChanged)

    Q_PROPERTY(bool remoteLoginEnabled
            READ remoteLoginEnabled
            NOTIFY remoteLoginEnabledChanged)

public:
    explicit DeveloperModeSettings(QObject *parent = NULL);
    virtual ~DeveloperModeSettings();

    const QString wlanIpAddress() const;
    const QString usbIpAddress() const;
    bool developerModeEnabled() const;
    bool remoteLoginEnabled() const;

    Q_INVOKABLE void setDeveloperMode(bool enabled);
    Q_INVOKABLE void setRemoteLogin(bool enabled);
    Q_INVOKABLE void setUsbIpAddress(const QString &usbIpAddress);

signals:
    void wlanIpAddressChanged();
    void usbIpAddressChanged();
    void developerModeEnabledChanged();
    void remoteLoginEnabledChanged();

private:
    QString m_wlanIpAddress;
    QString m_usbIpAddress;
    bool m_developerModeEnabled;
    bool m_remoteLoginEnabled;
};

#endif /* DEVELOPERMODESETTINGS_H */
