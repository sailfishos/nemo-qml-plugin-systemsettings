/*
 * Copyright (C) 2017 Jolla Ltd.
 * Contact: Raine Makelainen <raine.makelainen@jolla.com>
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

#ifndef BATTERYSTATUS_H
#define BATTERYSTATUS_H

#include <QObject>

#include <systemsettingsglobal.h>

class BatteryStatusPrivate;
class SYSTEMSETTINGS_EXPORT BatteryStatus : public QObject
{
    Q_OBJECT
    Q_ENUMS(ChargingMode)
    Q_ENUMS(ChargerStatus)
    Q_ENUMS(Status)

    Q_PROPERTY(ChargingMode chargingMode READ chargingMode WRITE setChargingMode
            NOTIFY chargingModeChanged)
    Q_PROPERTY(bool chargingForced READ chargingForced WRITE setChargingForced
            NOTIFY chargingForcedChanged)
    Q_PROPERTY(ChargerStatus chargerStatus READ chargerStatus NOTIFY chargerStatusChanged)
    Q_PROPERTY(int chargePercentage READ chargePercentage NOTIFY chargePercentageChanged)
    Q_PROPERTY(int chargeEnableLimit READ chargeEnableLimit WRITE setChargeEnableLimit
            NOTIFY chargeEnableLimitChanged)
    Q_PROPERTY(int chargeDisableLimit READ chargeDisableLimit WRITE setChargeDisableLimit
            NOTIFY chargeDisableLimitChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)

public:
    BatteryStatus(QObject *parent = 0);
    ~BatteryStatus();

    enum ChargingMode {
        EnableCharging,
        DisableCharging,
        ApplyChargingThresholds,
        ApplyChargingThresholdsAfterFull
    };
    Q_ENUM(ChargingMode)

    enum ChargerStatus {
        ChargerStatusUnknown = -1,
        Disconnected = 0,
        Connected = 1
    };
    Q_ENUM(ChargerStatus)

    enum Status {
        BatteryStatusUnknown = -1,
        Full = 0,
        Normal = 1,
        Low = 2,
        Empty = 3
    };
    Q_ENUM(Status)

    ChargingMode chargingMode() const;
    bool chargingForced() const;
    void setChargingMode(ChargingMode mode);
    void setChargingForced(bool forced);
    ChargerStatus chargerStatus() const;
    int chargePercentage() const;
    int chargeEnableLimit() const;
    void setChargeEnableLimit(int percentage);
    int chargeDisableLimit() const;
    void setChargeDisableLimit(int percentage);
    Status status() const;

signals:
    void chargingModeChanged(ChargingMode mode);
    void chargingForcedChanged(bool forced);
    void chargerStatusChanged(ChargerStatus status);
    void chargePercentageChanged(int percentage);
    void chargeEnableLimitChanged(int percentage);
    void chargeDisableLimitChanged(int percentage);
    void statusChanged(Status status);

private:
    BatteryStatusPrivate *d_ptr;
    Q_DISABLE_COPY(BatteryStatus)
    Q_DECLARE_PRIVATE(BatteryStatus)
};

#endif // BATTERYSTATUS_H
