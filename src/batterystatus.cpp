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

#include "batterystatus.h"
#include "batterystatus_p.h"

#include <QDBusServiceWatcher>
#include <QDBusConnection>

#include <mce/dbus-names.h>
#include <mce/mode-names.h>

BatteryStatusPrivate::BatteryStatusPrivate(BatteryStatus *batteryInfo)
    : QObject(batteryInfo)
    , q(batteryInfo)
    , status(BatteryStatus::BatteryStatusUnknown)
    , chargerStatus(BatteryStatus::ChargerStatusUnknown)
    , chargePercentage(-1)
    , m_connection(QDBusConnection::systemBus())
    , m_mceInterface(this, m_connection, MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF)
{
    QDBusServiceWatcher *mceWatcher = new QDBusServiceWatcher(MCE_SERVICE, QDBusConnection::systemBus(),
                                                              QDBusServiceWatcher::WatchForOwnerChange, this);

    connect(mceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, &BatteryStatusPrivate::mceRegistered);
    connect(mceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &BatteryStatusPrivate::mceUnregistered);

    registerSignals();

    // read initial values
    NemoDBus::Response *chargerState = m_mceInterface.call(MCE_CHARGER_STATE_GET);
    chargerState->onFinished<QString>([this](const QString &value) {
        chargerStatusChanged(value);
    });
    chargerState->onError([this](const QDBusError &error) {
        if (error.type() == QDBusError::ServiceUnknown) {
            // Service unknown => mce not registered. Signal initial state.
            emit q->chargerStatusChanged(BatteryStatus::ChargerStatusUnknown);
        }
    });

    NemoDBus::Response *batteryState = m_mceInterface.call(MCE_BATTERY_STATUS_GET);
    batteryState->onFinished<QString>([this](const QString &value) {
        statusChanged(value);
    });
    batteryState->onError([this](const QDBusError &error) {
        if (error.type() == QDBusError::ServiceUnknown) {
            // Service unknown => mce not registered. Signal initial state.
            emit q->statusChanged(BatteryStatus::BatteryStatusUnknown);
        }
    });

    NemoDBus::Response *batteryLevel = m_mceInterface.call(MCE_BATTERY_LEVEL_GET);
    batteryLevel->onFinished<int>([this](int value) {
        chargePercentageChanged(value);
    });
    batteryLevel->onError([this](const QDBusError &error) {
        if (error.type() == QDBusError::ServiceUnknown) {
            // Service unknown => mce not registered. Signal initial state.
            emit q->chargePercentageChanged(-1);
        }
    });
}

BatteryStatusPrivate::~BatteryStatusPrivate()
{
}

void BatteryStatusPrivate::registerSignals()
{
    m_connection.connectToSignal(
            MCE_SERVICE, MCE_SIGNAL_PATH,
            MCE_SIGNAL_IF, MCE_CHARGER_STATE_SIG,
            this, SLOT(chargerStatusChanged(QString)));
    m_connection.connectToSignal(
            MCE_SERVICE, MCE_SIGNAL_PATH,
            MCE_SIGNAL_IF, MCE_BATTERY_STATUS_SIG,
            this, SLOT(statusChanged(QString)));
    m_connection.connectToSignal(
            MCE_SERVICE, MCE_SIGNAL_PATH,
            MCE_SIGNAL_IF, MCE_BATTERY_LEVEL_SIG,
            this, SLOT(chargePercentageChanged(int)));
}

BatteryStatus::ChargerStatus BatteryStatusPrivate::parseChargerStatus(const QString &state)
{
    if (state == QLatin1String(MCE_CHARGER_STATE_ON)) {
        return BatteryStatus::Connected;
    } else if (state == QLatin1String(MCE_CHARGER_STATE_OFF)){
        return BatteryStatus::Disconnected;
    }

    return BatteryStatus::ChargerStatusUnknown;
}

BatteryStatus::Status BatteryStatusPrivate::parseBatteryStatus(const QString &status)
{
    if (status == QLatin1String(MCE_BATTERY_STATUS_LOW)) {
        return BatteryStatus::Low;
    } else if (status == QLatin1String(MCE_BATTERY_STATUS_OK)) {
        return BatteryStatus::Normal;
    } else if (status == QLatin1String(MCE_BATTERY_STATUS_EMPTY)) {
        return BatteryStatus::Empty;
    } else if (status == QLatin1String(MCE_BATTERY_STATUS_FULL)) {
        return BatteryStatus::Full;
    }

    return BatteryStatus::BatteryStatusUnknown;
}

void BatteryStatusPrivate::mceRegistered()
{
    registerSignals();
}

void BatteryStatusPrivate::mceUnregistered()
{
    chargerStatusChanged(QLatin1String(MCE_CHARGER_STATE_UNKNOWN));
    statusChanged(QLatin1String(MCE_BATTERY_STATUS_UNKNOWN));
    chargePercentageChanged(-1);
}

void BatteryStatusPrivate::chargerStatusChanged(const QString &status)
{
    BatteryStatus::ChargerStatus newStatus = parseChargerStatus(status);
    if (newStatus != chargerStatus) {
        chargerStatus = newStatus;
        emit q->chargerStatusChanged(chargerStatus);
    }
}

void BatteryStatusPrivate::statusChanged(const QString &s)
{
    BatteryStatus::Status newStatus = parseBatteryStatus(s);
    if (newStatus != status) {
        status = newStatus;
        emit q->statusChanged(status);
    }
}

void BatteryStatusPrivate::chargePercentageChanged(int percentage)
{
    if (percentage != chargePercentage) {
        chargePercentage = percentage;
        emit q->chargePercentageChanged(chargePercentage);
    }
}

BatteryStatus::BatteryStatus(QObject *parent)
    : QObject(parent)
    , d_ptr(new BatteryStatusPrivate(this))
{
}

BatteryStatus::~BatteryStatus()
{
}

/**
 * @brief BatteryStatus::chargerStatus
 * @return Returns charger connected status. Returns Connected when power cord is connected (charging) and
 * Disconnected when charger is disconnected (discharging). In case information cannot be read ChargerStatusUnknown
 * is returned.
 */
BatteryStatus::ChargerStatus BatteryStatus::chargerStatus() const
{
    Q_D(const BatteryStatus);
    return d->chargerStatus;
}

/**
 * @brief BatteryStatus::batteryChargeLevel
 * @return Returns battery charge level, in case information cannot be read -1 is returned.
 */
int BatteryStatus::chargePercentage() const
{
    Q_D(const BatteryStatus);
    return d->chargePercentage;
}


/**
 * @brief BatteryStatus::status
 * @return Returns battery charge status
 */
BatteryStatus::Status BatteryStatus::status() const
{
    Q_D(const BatteryStatus);
    return d->status;
}
