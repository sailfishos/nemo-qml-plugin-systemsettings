/*
 * Copyright (C) 2013 Jolla Ltd. <pekka.vuorela@jollamobile.com>
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

#ifndef USBSETTINGS_H
#define USBSETTINGS_H

#include <qmusbmode.h>

class USBSettings: public QObject
{
    Q_OBJECT

    Q_ENUMS(Mode)

    Q_PROPERTY(Mode currentMode READ currentMode NOTIFY currentModeChanged)
    Q_PROPERTY(Mode defaultMode READ defaultMode WRITE setDefaultMode NOTIFY defaultModeChanged)
    Q_PROPERTY(QList<int> supportedUSBModes READ supportedUSBModes NOTIFY supportedUSBModesChanged)

public:
    enum Mode {
        Connected = MeeGo::QmUSBMode::Connected,
        DataInUse = MeeGo::QmUSBMode::DataInUse,
        Disconnected = MeeGo::QmUSBMode::Disconnected,
        MassStorage = MeeGo::QmUSBMode::MassStorage,
        ChargingOnly = MeeGo::QmUSBMode::ChargingOnly,
        OviSuite = MeeGo::QmUSBMode::OviSuite,
        ModeRequest = MeeGo::QmUSBMode::ModeRequest,
        Ask = MeeGo::QmUSBMode::Ask,
        Undefined = MeeGo::QmUSBMode::Undefined,
        SDK = MeeGo::QmUSBMode::SDK,
        Developer = MeeGo::QmUSBMode::Developer,
        MTP = MeeGo::QmUSBMode::MTP,
        Adb = MeeGo::QmUSBMode::Adb,
        Diag = MeeGo::QmUSBMode::Diag
    };

    explicit USBSettings(QObject *parent = 0);
    virtual ~USBSettings();

    Mode currentMode() const;
    Mode defaultMode() const;
    QList<int> supportedUSBModes() const;

public slots:
    void setDefaultMode(const Mode mode);

signals:
    void currentModeChanged();
    void defaultModeChanged();
    void supportedUSBModesChanged();

private:
    MeeGo::QmUSBMode *m_qmmode;

    QList<int> m_supportedUSBModes;
};

#endif
