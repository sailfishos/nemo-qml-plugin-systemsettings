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

#include "displaysettings.h"

#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include "mceiface.h"
#include <MGConfItem>
#include <QDebug>

static const char *MceMaxDisplayBrightness = "/system/osso/dsm/display/max_display_brightness_levels";
static const char *MceDisplayBrightness = "/system/osso/dsm/display/display_brightness";
static const char *MceDisplayDimTimeout = "/system/osso/dsm/display/display_dim_timeout";
static const char *MceDisplayBlankTimeout = "/system/osso/dsm/display/display_blank_timeout";
static const char *MceDisplayInhibitMode = "/system/osso/dsm/display/inhibit_blank_mode";
static const char *MceDisplayUseAdaptiveDimming = "/system/osso/dsm/display/use_adaptive_display_dimming";
static const char *MceDisplayUseLowPowerMode = "/system/osso/dsm/display/use_low_power_mode";
static const char *MceDisplayUseAmbientLightSensor = "/system/osso/dsm/display/als_enabled";
static const char *MceDisplayAutoBrightnessEnabled = "/system/osso/dsm/display/als_autobrightness";
static const char *MceDoubleTapMode = "/system/osso/dsm/doubletap/mode";
static const char *MceLidSensorEnabled = "/system/osso/dsm/locks/lid_sensor_enabled";
static const char *MceLidSensorFilteringEnabled = "/system/osso/dsm/locks/filter_lid_with_als";
static const char *MceFlipOverGestureEnabled = "/system/osso/dsm/display/flipover_gesture_enabled";
static const char *McePowerSaveModeForced = "/system/osso/dsm/energymanagement/force_power_saving";
static const char *McePowerSaveModeEnabled = "/system/osso/dsm/energymanagement/enable_power_saving";
static const char *McePowerSaveModeThreshold = "/system/osso/dsm/energymanagement/psm_threshold";

DisplaySettings::DisplaySettings(QObject *parent)
    : QObject(parent)
{
    m_orientationLock = new MGConfItem("/lipstick/orientationLock", this);
    connect(m_orientationLock, &MGConfItem::valueChanged,
            this, &DisplaySettings::orientationLockChanged);

    /* Initialize to defaults */
    m_autoBrightnessEnabled     = true;
    m_ambientLightSensorEnabled = true;
    m_blankTimeout              = 3;
    m_maxBrightness             = 100;
    m_brightness                = 60;
    m_dimTimeout                = 30;
    m_flipoverGestureEnabled    = true;
    m_inhibitMode               = InhibitOff;
    m_adaptiveDimmingEnabled    = true;
    m_lowPowerModeEnabled       = false;
    m_doubleTapMode             = true;
    m_lidSensorFilteringEnabled = true;
    m_lidSensorEnabled          = true;
    m_powerSaveModeForced       = false;
    m_powerSaveModeEnabled      = false;
    m_powerSaveModeThreshold    = 20;
    m_populated                 = false;

    /* Setup change listener & get current values via async query */
    m_mceSignalIface = new ComNokiaMceSignalInterface(MCE_SERVICE, MCE_SIGNAL_PATH,
                                                      QDBusConnection::systemBus(), this);
    connect(m_mceSignalIface, SIGNAL(config_change_ind(QString,QDBusVariant)),
            this, SLOT(configChange(QString,QDBusVariant)));

    m_mceIface = new ComNokiaMceRequestInterface(MCE_SERVICE, MCE_REQUEST_PATH,
                                                 QDBusConnection::systemBus(), this);
    QDBusPendingReply<QVariantMap> call = m_mceIface->get_config_all();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher *)),
                     this, SLOT(configReply(QDBusPendingCallWatcher *)));
}

void DisplaySettings::configReply(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not retrieve mce settings: '%s'",
                 reply.error().message().toStdString().c_str());
    } else {
        QVariantMap map = reply.value();
        QMapIterator<QString, QVariant> i(map);
        while (i.hasNext()) {
            i.next();
            updateConfig(i.key(), i.value());
        }
        m_populated = true;
        emit populatedChanged();
    }

    watcher->deleteLater();
}

int DisplaySettings::brightness() const
{
    return m_brightness;
}

void DisplaySettings::setBrightness(int value)
{
    if (m_brightness != value) {
        m_brightness = value;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayBrightness), QDBusVariant(value));
        emit brightnessChanged();
    }
}

int DisplaySettings::maximumBrightness()
{
    return m_maxBrightness;
}

int DisplaySettings::dimTimeout() const
{
    return m_dimTimeout;
}

void DisplaySettings::setDimTimeout(int value)
{
    if (m_dimTimeout != value) {
        m_dimTimeout = value;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayDimTimeout), QDBusVariant(value));
        emit dimTimeoutChanged();
    }
}

int DisplaySettings::blankTimeout() const
{
    return m_blankTimeout;
}

void DisplaySettings::setBlankTimeout(int value)
{
    if (m_blankTimeout != value) {
        m_blankTimeout = value;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayBlankTimeout), QDBusVariant(value));
        emit blankTimeoutChanged();
    }
}

DisplaySettings::InhibitMode DisplaySettings::inhibitMode() const
{
    return m_inhibitMode;
}

void DisplaySettings::setInhibitMode(InhibitMode mode)
{
    if (m_inhibitMode != mode) {
        m_inhibitMode = mode;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayInhibitMode), QDBusVariant(static_cast<int>(mode)));
        emit inhibitModeChanged();
    }
}

bool DisplaySettings::adaptiveDimmingEnabled() const
{
    return m_adaptiveDimmingEnabled;
}

void DisplaySettings::setAdaptiveDimmingEnabled(bool enabled)
{
    if (m_adaptiveDimmingEnabled != enabled) {
        m_adaptiveDimmingEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayUseAdaptiveDimming), QDBusVariant(enabled));
        emit adaptiveDimmingEnabledChanged();
    }
}

bool DisplaySettings::lowPowerModeEnabled() const
{
    return m_lowPowerModeEnabled;
}

void DisplaySettings::setLowPowerModeEnabled(bool enabled)
{
    if (m_lowPowerModeEnabled != enabled) {
        m_lowPowerModeEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayUseLowPowerMode), QDBusVariant(enabled));
        emit lowPowerModeEnabledChanged();
    }
}

bool DisplaySettings::ambientLightSensorEnabled() const
{
    return m_ambientLightSensorEnabled;
}

void DisplaySettings::setAmbientLightSensorEnabled(bool enabled)
{
    if (m_ambientLightSensorEnabled != enabled) {
        m_ambientLightSensorEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayUseAmbientLightSensor), QDBusVariant(enabled));
        emit ambientLightSensorEnabledChanged();
    }
}

bool DisplaySettings::autoBrightnessEnabled() const
{
    return m_autoBrightnessEnabled;
}

void DisplaySettings::setAutoBrightnessEnabled(bool enabled)
{
    if (m_autoBrightnessEnabled != enabled) {
        m_autoBrightnessEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceDisplayAutoBrightnessEnabled), QDBusVariant(enabled));
        emit autoBrightnessEnabledChanged();
    }
}

int DisplaySettings::doubleTapMode() const
{
    return m_doubleTapMode;
}

void DisplaySettings::setDoubleTapMode(int mode)
{
    if (m_doubleTapMode != mode) {
        m_doubleTapMode = mode;
        m_mceIface->set_config(QDBusObjectPath(MceDoubleTapMode), QDBusVariant(mode));
        emit doubleTapModeChanged();
    }
}

QVariant DisplaySettings::orientationLock() const
{
    return m_orientationLock->value("dynamic");
}

void DisplaySettings::setOrientationLock(const QVariant &orientationLock)
{
    m_orientationLock->set(orientationLock);
}

bool DisplaySettings::lidSensorEnabled() const
{
    return m_lidSensorEnabled;
}

void DisplaySettings::setLidSensorEnabled(bool enabled)
{
    if (m_lidSensorEnabled != enabled) {
        m_lidSensorEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceLidSensorEnabled), QDBusVariant(enabled));
        emit lidSensorEnabledChanged();
    }
}

bool DisplaySettings::lidSensorFilteringEnabled() const
{
    return m_lidSensorFilteringEnabled;
}

void DisplaySettings::setLidSensorFilteringEnabled(bool enabled)
{
    if (m_lidSensorFilteringEnabled != enabled) {
        m_lidSensorFilteringEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceLidSensorFilteringEnabled), QDBusVariant(enabled));
        emit lidSensorFilteringEnabledChanged();
    }
}
bool DisplaySettings::flipoverGestureEnabled() const
{
    return m_flipoverGestureEnabled;
}

void DisplaySettings::setFlipoverGestureEnabled(bool enabled)
{
    if (m_flipoverGestureEnabled != enabled) {
        m_flipoverGestureEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(MceFlipOverGestureEnabled), QDBusVariant(enabled));
        emit flipoverGestureEnabledChanged();
    }
}

bool DisplaySettings::powerSaveModeForced() const
{
    return m_powerSaveModeForced;
}

void DisplaySettings::setPowerSaveModeForced(bool force)
{
    if (m_powerSaveModeForced != force) {
        m_powerSaveModeForced = force;
        m_mceIface->set_config(QDBusObjectPath(McePowerSaveModeForced), QDBusVariant(force));
        emit powerSaveModeForcedChanged();
    }
}

bool DisplaySettings::powerSaveModeEnabled() const
{
    return m_powerSaveModeEnabled;
}

void DisplaySettings::setPowerSaveModeEnabled(bool enabled)
{
    if (m_powerSaveModeEnabled != enabled) {
        m_powerSaveModeEnabled = enabled;
        m_mceIface->set_config(QDBusObjectPath(McePowerSaveModeEnabled), QDBusVariant(enabled));
        emit powerSaveModeEnabledChanged();
    }
}

int DisplaySettings::powerSaveModeThreshold() const
{
    return m_powerSaveModeThreshold;
}

void DisplaySettings::setPowerSaveModeThreshold(int value)
{
    if (m_powerSaveModeThreshold != value) {
        m_powerSaveModeThreshold = value;
        m_mceIface->set_config(QDBusObjectPath(McePowerSaveModeThreshold), QDBusVariant(value));
        emit powerSaveModeThresholdChanged();
    }
}

bool DisplaySettings::populated() const
{
    return m_populated;
}

void DisplaySettings::configChange(const QString &key, const QDBusVariant &value)
{
    updateConfig(key, value.variant());
}

void DisplaySettings::updateConfig(const QString &key, const QVariant &value)
{
    if (key == MceDisplayBrightness) {
        int val = value.toInt();
        if (val != m_brightness) {
            m_brightness = val;
            emit brightnessChanged();
        }
    } else if (key == MceDisplayDimTimeout) {
        int val = value.toInt();
        if (val != m_dimTimeout) {
            m_dimTimeout = val;
            emit dimTimeoutChanged();
        }
    } else if (key == MceDisplayBlankTimeout) {
        int val = value.toInt();
        if (val != m_blankTimeout) {
            m_blankTimeout = val;
            emit blankTimeoutChanged();
        }
    } else if (key == MceDisplayInhibitMode) {
        InhibitMode val = static_cast<InhibitMode>(value.toInt());
        if (val != m_inhibitMode) {
            m_inhibitMode = val;
            emit inhibitModeChanged();
        }
    } else if (key == MceDisplayUseAdaptiveDimming) {
        bool val = value.toBool();
        if (val != m_adaptiveDimmingEnabled) {
            m_adaptiveDimmingEnabled = val;
            emit adaptiveDimmingEnabledChanged();
        }
    } else if (key == MceDisplayUseLowPowerMode) {
        bool val = value.toBool();
        if (val != m_lowPowerModeEnabled) {
            m_lowPowerModeEnabled = val;
            emit lowPowerModeEnabledChanged();
        }
    } else if (key == MceDisplayUseAmbientLightSensor) {
        bool val = value.toBool();
        if (val != m_ambientLightSensorEnabled) {
            m_ambientLightSensorEnabled = val;
            emit ambientLightSensorEnabledChanged();
        }
    } else if (key == MceDisplayAutoBrightnessEnabled) {
        bool val = value.toBool();
        if (val != m_autoBrightnessEnabled) {
            m_autoBrightnessEnabled = val;
            emit autoBrightnessEnabledChanged();
        }
    } else if (key == MceDoubleTapMode) {
        int val = value.toInt();
        if (val != m_doubleTapMode) {
            m_doubleTapMode = val;
            emit doubleTapModeChanged();
        }
    } else if (key == MceLidSensorEnabled) {
        bool val = value.toBool();
        if (val != m_lidSensorEnabled) {
            m_lidSensorEnabled = val;
            emit lidSensorEnabledChanged();
        }
    } else if (key == MceLidSensorFilteringEnabled) {
        bool val = value.toBool();
        if (val != m_lidSensorFilteringEnabled) {
            m_lidSensorFilteringEnabled = val;
            emit lidSensorFilteringEnabledChanged();
        }
    } else if (key == MceFlipOverGestureEnabled) {
        bool val = value.toBool();
        if (val != m_flipoverGestureEnabled) {
            m_flipoverGestureEnabled = val;
            emit flipoverGestureEnabledChanged();
        }
    } else if (key == McePowerSaveModeForced) {
        bool val = value.toBool();
        if (val != m_powerSaveModeForced) {
            m_powerSaveModeForced = val;
            emit powerSaveModeForcedChanged();
        }
    } else if (key == McePowerSaveModeEnabled) {
        bool val = value.toBool();
        if (val != m_powerSaveModeEnabled) {
            m_powerSaveModeEnabled = val;
            emit powerSaveModeEnabledChanged();
        }
    } else if (key == McePowerSaveModeThreshold) {
        int val = value.toInt();
        if (val != m_powerSaveModeThreshold) {
            m_powerSaveModeThreshold = val;
            emit powerSaveModeThresholdChanged();
        }
    } else if (key == MceMaxDisplayBrightness) {
        int val = value.toInt();
        if (val != m_maxBrightness) {
            m_maxBrightness = val;
            emit maximumBrightnessChanged();
        }
    }
}
