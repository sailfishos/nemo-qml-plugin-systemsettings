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

#include <QObject>
#include <QList>

class USBSettings: public QObject
{
    Q_OBJECT

    Q_ENUMS(Mode)

    Q_PROPERTY(Mode currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(Mode defaultMode READ defaultMode WRITE setDefaultMode NOTIFY defaultModeChanged)
    Q_PROPERTY(QList<int> supportedUSBModes READ supportedUSBModes NOTIFY supportedUSBModesChanged)

public:
    /**
     * Keep this in sync with usb_moded-dbus.h (for states) and usb_moded-modes.h (for modes),
     * existing enum values taken from legacy qmsystem2 enum mapping for compatibility
     **/
    enum Mode {
        // States (from usb_moded-dbus.h)
        Connected = 0,
        DataInUse = 1,
        Disconnected = 2,
        ModeRequest = 6,

        // Modes (from usb_moded-modes.h)
        MassStorage = 3,
        ChargingOnly = 4,
        PCSuite = 5,
        Ask = 7,
        Undefined = 8,
        Developer = 10,
        MTP = 11,
        Adb = 12,
        Diag = 13,
        ConnectionSharing = 14,
        Host = 15,
        Charger = 16,

        // When adding new Mode/State IDs, start with 50 (assume 0-49 was used by qmsystem2)
    };

    explicit USBSettings(QObject *parent = 0);
    virtual ~USBSettings();

    Mode currentMode() const;
    Mode defaultMode() const;
    QList<int> supportedUSBModes() const;

public slots:
    void setDefaultMode(const Mode mode);
    void setCurrentMode(const Mode mode);

signals:
    void currentModeChanged();
    void defaultModeChanged();
    void supportedUSBModesChanged();

private:
    QList<int> m_supportedUSBModes;
};

#endif
