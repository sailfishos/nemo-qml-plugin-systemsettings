/*
 * Copyright (C) 2013 - 2019 Jolla Ltd.
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef ABOUTSETTINGS_H
#define ABOUTSETTINGS_H

#include <QObject>
#include <QVariant>

#include <systemsettingsglobal.h>

class AboutSettingsPrivate;

class SYSTEMSETTINGS_EXPORT AboutSettings: public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString wlanMacAddress READ wlanMacAddress CONSTANT)
    Q_PROPERTY(QString imei READ imei CONSTANT)
    Q_PROPERTY(QString serial READ serial CONSTANT)
    Q_PROPERTY(QString localizedOperatingSystemName READ localizedOperatingSystemName CONSTANT)
    Q_PROPERTY(QString baseOperatingSystemName READ baseOperatingSystemName CONSTANT)
    Q_PROPERTY(QString operatingSystemName READ operatingSystemName CONSTANT)
    Q_PROPERTY(QString localizedSoftwareVersion READ localizedSoftwareVersion CONSTANT)
    Q_PROPERTY(QString softwareVersion READ softwareVersion CONSTANT)
    Q_PROPERTY(QString softwareVersionId READ softwareVersionId CONSTANT)
    Q_PROPERTY(QString adaptationVersion READ adaptationVersion CONSTANT)
    Q_PROPERTY(QString vendorName READ vendorName CONSTANT)
    Q_PROPERTY(QString vendorVersion READ vendorVersion CONSTANT)

public:
    explicit AboutSettings(QObject *parent = 0);
    virtual ~AboutSettings();

    QString wlanMacAddress() const;
    QString imei() const;
    QString serial() const;
    QString localizedOperatingSystemName() const;
    QString baseOperatingSystemName() const;
    QString operatingSystemName() const;
    QString localizedSoftwareVersion() const;
    QString softwareVersion() const;
    QString softwareVersionId() const;
    QString adaptationVersion() const;

    QString vendorName() const;
    QString vendorVersion() const;

private:
    Q_DECLARE_PRIVATE(AboutSettings)
    Q_DISABLE_COPY(AboutSettings)

    AboutSettingsPrivate *d_ptr;
};

#endif
