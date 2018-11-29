/*
 * Copyright (C) 2017 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
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

#include "locationsettings.h"
#include "locationsettings_p.h"

#include <QFile>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QDebug>

#include <sailfishkeyprovider.h>
#include <sailfishkeyprovider_iniparser.h>
#include <sailfishkeyprovider_processmutex.h>

#include <networkmanager.h>
#include <networktechnology.h>

#include <limits>

namespace {
// TODO: replace all of this with DBus calls to a central settings service...
QString boolToString(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }
const QString LocationSettingsDir = QStringLiteral("/etc/location/");
const QString LocationSettingsFile = QStringLiteral("/etc/location/location.conf");
const QString LocationSettingsKeys = QStringLiteral(
                    "enabled"                               ";"
                    "allowed_data_sources\\online"          ";"
                    "allowed_data_sources\\device_sensors"  ";"
                    "allowed_data_sources\\bt_data"         ";"
                    "allowed_data_sources\\wlan_data"       ";"
                    "allowed_data_sources\\cell_data"       ";"
                    "allowed_data_sources\\gps"             ";"
                    "allowed_data_sources\\glonass"         ";"
                    "allowed_data_sources\\beidou"          ";"
                    "allowed_data_sources\\galileo"         ";"
                    "allowed_data_sources\\qzss"            ";"
                    "allowed_data_sources\\sbas"            ";"
                    "custom_mode"                           ";"
                    "agps_providers"                        ";"
                    "gps\\enabled"                          ";"
                    "mls\\enabled"                          ";"
                    "mls\\agreement_accepted"               ";"
                    "mls\\online_enabled"                   ";"
                    "here\\enabled"                         ";"
                    "here\\agreement_accepted"              ";"
                    "here\\online_enabled"                  ";"
                    /* and the deprecated keys: */
                    "cell_id_positioning_enabled"           ";"
                    "here_agreement_accepted"               ";"
                    "agreement_accepted");
const int LocationSettingsValueIndex_Enabled = 0;
const int LocationSettingsValueIndex_AllowedDataSources_Online = 1;
const int LocationSettingsValueIndex_AllowedDataSources_DeviceSensors = 2;
const int LocationSettingsValueIndex_AllowedDataSources_Bluetooth = 3;
const int LocationSettingsValueIndex_AllowedDataSources_WlanData = 4;
const int LocationSettingsValueIndex_AllowedDataSources_CellData = 5;
const int LocationSettingsValueIndex_AllowedDataSources_Gps = 6;
const int LocationSettingsValueIndex_AllowedDataSources_Glonass = 7;
const int LocationSettingsValueIndex_AllowedDataSources_Beidou = 8;
const int LocationSettingsValueIndex_AllowedDataSources_Galileo = 9;
const int LocationSettingsValueIndex_AllowedDataSources_Qzss = 10;
const int LocationSettingsValueIndex_AllowedDataSources_Sbas = 11;
const int LocationSettingsValueIndex_CustomMode = 12;
const int LocationSettingsValueIndex_AgpsProviders = 13;
const int LocationSettingsValueIndex_Gps_Enabled = 14;
const int LocationSettingsValueIndex_Mls_Enabled = 15;
const int LocationSettingsValueIndex_Mls_AgreementAccepted = 16;
const int LocationSettingsValueIndex_Mls_OnlineEnabled = 17;
const int LocationSettingsValueIndex_Here_Enabled = 18;
const int LocationSettingsValueIndex_Here_AgreementAccepted = 19;
const int LocationSettingsValueIndex_Here_OnlineEnabled = 26;
const int LocationSettingsValueIndex_DEPRECATED_CellIdPositioningEnabled = 20;
const int LocationSettingsValueIndex_DEPRECATED_HereEnabled = 21;
const int LocationSettingsValueIndex_DEPRECATED_HereAgreementAccepted = 22;
QMap<int, LocationSettings::DataSource> LocationSettingsValueIndexToDataSourceMap
    {
        { LocationSettingsValueIndex_AllowedDataSources_Online,
          LocationSettings::OnlineDataSources },
        { LocationSettingsValueIndex_AllowedDataSources_DeviceSensors,
          LocationSettings::DeviceSensorsData },
        { LocationSettingsValueIndex_AllowedDataSources_Bluetooth,
          LocationSettings::BluetoothData },
        { LocationSettingsValueIndex_AllowedDataSources_WlanData,
          LocationSettings::WlanData },
        { LocationSettingsValueIndex_AllowedDataSources_CellData,
          LocationSettings::CellTowerData },
        { LocationSettingsValueIndex_AllowedDataSources_Gps,
          LocationSettings::GpsData },
        { LocationSettingsValueIndex_AllowedDataSources_Glonass,
          LocationSettings::GlonassData },
        { LocationSettingsValueIndex_AllowedDataSources_Beidou,
          LocationSettings::BeidouData },
        { LocationSettingsValueIndex_AllowedDataSources_Galileo,
          LocationSettings::GalileoData },
        { LocationSettingsValueIndex_AllowedDataSources_Qzss,
          LocationSettings::QzssData },
        { LocationSettingsValueIndex_AllowedDataSources_Sbas,
          LocationSettings::SbasData },
    };
const QString PoweredPropertyName = QStringLiteral("Powered");
}

LocationSettingsPrivate::LocationSettingsPrivate(LocationSettings::Mode mode, LocationSettings *settings)
    : QObject(settings)
    , q(settings)
    , m_locationEnabled(false)
    , m_gpsEnabled(false)
    , m_mlsEnabled(true)
    , m_mlsOnlineState(LocationSettings::OnlineAGpsAgreementNotAccepted)
    , m_hereState(LocationSettings::OnlineAGpsAgreementNotAccepted)
    , m_locationMode(LocationSettings::CustomMode)
    , m_settingLocationMode(true)
    , m_settingMultipleSettings(false)
    , m_allowedDataSources(static_cast<LocationSettings::DataSources>(std::numeric_limits<quint32>::max()))
    , m_connMan(Q_NULLPTR)
    , m_gpsTech(Q_NULLPTR)
    , m_gpsTechInterface(mode == LocationSettings::AsynchronousMode
                         ? Q_NULLPTR
                         : new QDBusInterface("net.connman",
                                              "/net/connman/technology/gps",
                                              "net.connman.Technology",
                                              QDBusConnection::systemBus()))
{
    connect(q, &LocationSettings::gpsEnabledChanged,
            this, &LocationSettingsPrivate::recalculateLocationMode);
    connect(q, &LocationSettings::mlsEnabledChanged,
            this, &LocationSettingsPrivate::recalculateLocationMode);
    connect(q, &LocationSettings::mlsOnlineStateChanged,
            this, &LocationSettingsPrivate::recalculateLocationMode);
    connect(q, &LocationSettings::hereStateChanged,
            this, &LocationSettingsPrivate::recalculateLocationMode);

    connect(&m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(readSettings()));
    connect(&m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(readSettings()));

    m_watcher.addPath(LocationSettingsDir);
    if (QFile(LocationSettingsFile).exists() && m_watcher.addPath(LocationSettingsFile)) {
        readSettings();
    } else {
        qWarning() << "Unable to follow location configuration file changes";
    }

    this->m_settingLocationMode = false;

    if (m_gpsTechInterface) {
        QDBusConnection::systemBus().connect("net.connman",
                                             "/net/connman/technology/gps",
                                             "net.connman.Technology",
                                             "PropertyChanged",
                                             this, SLOT(gpsTechPropertyChanged(QString,QVariant)));
    } else {
        m_connMan = NetworkManagerFactory::createInstance();
        connect(m_connMan, &NetworkManager::technologiesChanged,
                this, &LocationSettingsPrivate::findGpsTech);
        connect(m_connMan, &NetworkManager::availabilityChanged,
                this, &LocationSettingsPrivate::findGpsTech);
        findGpsTech();
    }
}

LocationSettingsPrivate::~LocationSettingsPrivate()
{
    if (m_gpsTech) {
        disconnect(m_gpsTech, 0, q, 0);
        m_gpsTech = 0;
    }
    if (m_gpsTechInterface) {
        delete m_gpsTechInterface;
        m_gpsTechInterface = 0;
    }
}

void LocationSettingsPrivate::gpsTechPropertyChanged(const QString &propertyName, const QVariant &)
{
    if (propertyName == PoweredPropertyName) {
        emit q->gpsFlightModeChanged();
    }
}

void LocationSettingsPrivate::findGpsTech()
{
    NetworkTechnology *newGpsTech = m_connMan->getTechnology(QStringLiteral("gps"));
    if (newGpsTech == m_gpsTech) {
        return; // no change.
    }
    if (m_gpsTech) {
        disconnect(m_gpsTech, 0, q, 0);
    }
    m_gpsTech = newGpsTech;
    if (m_gpsTech) {
        connect(m_gpsTech, &NetworkTechnology::poweredChanged,
                q, &LocationSettings::gpsFlightModeChanged);
    }
    emit q->gpsFlightModeChanged();
}

LocationSettings::LocationMode
LocationSettingsPrivate::calculateLocationMode() const
{
    if (m_gpsEnabled
            && (!mlsAvailable() ||
                    (m_mlsEnabled && m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled))
            && (!hereAvailable() || m_hereState == LocationSettings::OnlineAGpsEnabled)) {
        return LocationSettings::HighAccuracyMode;
    } else if (!m_gpsEnabled
            && (!mlsAvailable() ||
                    (m_mlsEnabled &&
                        (m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled
                        || m_mlsOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted)))
            && (!hereAvailable() ||
                    (m_hereState == LocationSettings::OnlineAGpsEnabled
                    || m_hereState == LocationSettings::OnlineAGpsAgreementNotAccepted))) {
        return LocationSettings::BatterySavingMode;
    } else if (m_gpsEnabled
            && (!mlsAvailable() ||
                    (m_mlsEnabled &&
                        (m_mlsOnlineState == LocationSettings::OnlineAGpsDisabled
                        || m_mlsOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted)))
            && (!hereAvailable() ||
                    (m_hereState == LocationSettings::OnlineAGpsDisabled
                    || m_hereState == LocationSettings::OnlineAGpsAgreementNotAccepted))) {
        return LocationSettings::DeviceOnlyMode;
    } else {
        return LocationSettings::CustomMode;
    }
}

void LocationSettingsPrivate::recalculateLocationMode()
{
    if (!m_settingLocationMode && m_locationMode != LocationSettings::CustomMode) {
        LocationSettings::LocationMode currentMode = calculateLocationMode();
        if (currentMode != m_locationMode) {
            m_locationMode = currentMode;
            emit q->locationModeChanged();
        }
    }
}

bool LocationSettingsPrivate::mlsAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-mlsdb"));
}

bool LocationSettingsPrivate::hereAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-here"));
}


LocationSettings::LocationSettings(QObject *parent)
    : QObject(parent)
    , d_ptr(new LocationSettingsPrivate(LocationSettings::AsynchronousMode, this))
{
}

LocationSettings::LocationSettings(LocationSettings::Mode mode, QObject *parent)
    : QObject(parent)
    , d_ptr(new LocationSettingsPrivate(mode, this))
{
}

LocationSettings::~LocationSettings()
{
}

bool LocationSettings::locationEnabled() const
{
    Q_D(const LocationSettings);
    return d->m_locationEnabled;
}

void LocationSettings::setLocationEnabled(bool enabled)
{
    Q_D(LocationSettings);
    if (enabled != d->m_locationEnabled) {
        d->m_locationEnabled = enabled;
        d->writeSettings();
        emit locationEnabledChanged();
    }
}

bool LocationSettings::gpsEnabled() const
{
    Q_D(const LocationSettings);
    return d->m_gpsEnabled;
}

void LocationSettings::setGpsEnabled(bool enabled)
{
    Q_D(LocationSettings);
    if (enabled != d->m_gpsEnabled) {
        d->m_gpsEnabled = enabled;
        d->writeSettings();
        emit gpsEnabledChanged();
    }
}

bool LocationSettings::gpsFlightMode() const
{
    Q_D(const LocationSettings);
    if (d->m_gpsTechInterface) {
        QDBusReply<QVariantMap> reply = d->m_gpsTechInterface->call("GetProperties");
        if (reply.error().isValid()) {
            qWarning() << reply.error().message();
        } else {
            QVariantMap props = reply.value();
            if (props.contains(PoweredPropertyName)) {
                return !props.value(PoweredPropertyName).toBool();
            } else {
                qWarning() << "Powered property not returned for GPS technology!";
            }
        }
        return false;
    }
    return d->m_gpsTech == Q_NULLPTR ? false : !(d->m_gpsTech->powered());
}

void LocationSettings::setGpsFlightMode(bool flightMode)
{
    Q_D(LocationSettings);
    if (d->m_gpsTechInterface) {
        QDBusReply<void> reply = d->m_gpsTechInterface->call("SetProperty",
                                                             PoweredPropertyName,
                                                             QVariant::fromValue<QDBusVariant>(QDBusVariant(QVariant::fromValue<bool>(!flightMode))));
        if (reply.error().isValid()) {
            qWarning() << reply.error().message();
        }
    } else if (d->m_gpsTech && d->m_gpsTech->powered() == flightMode) {
        d->m_gpsTech->setPowered(!flightMode);
    }
}

bool LocationSettings::gpsAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-hybris"));
}

bool LocationSettings::mlsEnabled() const
{
    Q_D(const LocationSettings);
    return d->m_mlsEnabled;
}

void LocationSettings::setMlsEnabled(bool enabled)
{
    Q_D(LocationSettings);
    if (enabled != d->m_mlsEnabled) {
        d->m_mlsEnabled = enabled;
        d->writeSettings();
        emit mlsEnabledChanged();
    }
}

LocationSettings::OnlineAGpsState LocationSettings::mlsOnlineState() const
{
    Q_D(const LocationSettings);
    return d->m_mlsOnlineState;
}

void LocationSettings::setMlsOnlineState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    if (state == d->m_mlsOnlineState)
        return;

    d->m_mlsOnlineState = state;
    d->writeSettings();
    emit mlsOnlineStateChanged();
}

bool LocationSettings::mlsAvailable() const
{
    Q_D(const LocationSettings);
    return d->mlsAvailable();
}

LocationSettings::OnlineAGpsState LocationSettings::hereState() const
{
    Q_D(const LocationSettings);
    return d->m_hereState;
}

void LocationSettings::setHereState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    if (state == d->m_hereState)
        return;

    d->m_hereState = state;
    d->writeSettings();
    emit hereStateChanged();
}

bool LocationSettings::hereAvailable() const
{
    Q_D(const LocationSettings);
    return d->hereAvailable();
}

LocationSettings::LocationMode LocationSettings::locationMode() const
{
    Q_D(const LocationSettings);
    return d->m_locationMode;
}

void LocationSettings::setLocationMode(LocationMode locationMode)
{
    Q_D(LocationSettings);

    LocationSettings::LocationMode oldLocationMode = this->locationMode();
    if (oldLocationMode == locationMode) {
        return;
    }

    d->m_settingLocationMode = true;
    d->m_settingMultipleSettings = true;
    d->m_locationMode = locationMode;

    if (locationMode == HighAccuracyMode) {
        setGpsEnabled(true);
        if (mlsAvailable()) {
            setMlsEnabled(true);
            if (mlsOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setMlsOnlineState(LocationSettings::OnlineAGpsEnabled);
            }
        }
        if (hereAvailable()) {
            if (hereState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setHereState(LocationSettings::OnlineAGpsEnabled);
            }
        }
    } else if (locationMode == BatterySavingMode) {
        setGpsEnabled(false);
        if (mlsAvailable()) {
            setMlsEnabled(true);
            if (mlsOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setMlsOnlineState(LocationSettings::OnlineAGpsEnabled);
            }
        }
        if (hereAvailable()) {
            if (hereState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setHereState(LocationSettings::OnlineAGpsEnabled);
            }
        }
    } else if (locationMode == DeviceOnlyMode) {
        setGpsEnabled(true);
        if (mlsAvailable()) {
            setMlsEnabled(true);
            if (mlsOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setMlsOnlineState(LocationSettings::OnlineAGpsDisabled);
            }
        }
        if (hereAvailable()) {
            if (hereState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setHereState(LocationSettings::OnlineAGpsDisabled);
            }
        }
    }

    d->m_settingMultipleSettings = false;
    d->writeSettings();
    emit locationModeChanged();

    d->m_settingLocationMode = false;
}

LocationSettings::DataSources LocationSettings::allowedDataSources() const
{
    Q_D(const LocationSettings);
    return d->m_allowedDataSources;
}

void LocationSettings::setAllowedDataSources(LocationSettings::DataSources dataSources)
{
    Q_D(LocationSettings);
    if (dataSources == d->m_allowedDataSources)
        return;

    d->m_allowedDataSources = dataSources;
    d->writeSettings();
    emit allowedDataSourcesChanged();
}

void LocationSettingsPrivate::readSettings()
{
    if (!m_processMutex) {
        m_processMutex.reset(new Sailfish::KeyProvider::ProcessMutex(LocationSettingsFile.toLatin1().constData()));
    }

    m_processMutex->lock();
    char **locationSettingsValues = SailfishKeyProvider_ini_read_multiple(
                "/etc/location/location.conf",
                "location",
                LocationSettingsKeys.toLatin1().constData(),
                ";");
    m_processMutex->unlock();

    if (locationSettingsValues == NULL) {
        qWarning() << "Unable to read location configuration settings!";
        return;
    }

    // read the deprecated keys first, for compatibility purposes:
    bool oldMlsEnabled = locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_CellIdPositioningEnabled] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_CellIdPositioningEnabled], "true") == 0;
    bool oldHereEnabled = locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_HereEnabled] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_HereEnabled], "true") == 0;
    bool oldHereAgreementAccepted = locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_HereAgreementAccepted] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_DEPRECATED_HereAgreementAccepted], "true") == 0;

    // then read the new key values (overriding with deprecated values if needed):
    bool locationEnabled = locationSettingsValues[LocationSettingsValueIndex_Enabled] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_Enabled], "true") == 0;
    bool customMode = locationSettingsValues[LocationSettingsValueIndex_CustomMode] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_CustomMode], "true") == 0;
    LocationSettings::DataSources allowedDataSources = static_cast<LocationSettings::DataSources>(std::numeric_limits<quint32>::max());
    for (QMap<int, LocationSettings::DataSource>::const_iterator it = LocationSettingsValueIndexToDataSourceMap.constBegin();
            it != LocationSettingsValueIndexToDataSourceMap.constEnd(); it++) {
        if (locationSettingsValues[it.key()] != NULL && strcmp(locationSettingsValues[it.key()], "true") != 0) {
            allowedDataSources &= ~it.value(); // mark the data source as disabled
        }
    }
    // skip over the agps_providers value.
    bool gpsEnabled = locationSettingsValues[LocationSettingsValueIndex_Gps_Enabled] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_Gps_Enabled], "true") == 0;
    bool mlsEnabled = oldMlsEnabled
            || (locationSettingsValues[LocationSettingsValueIndex_Mls_Enabled] != NULL
                    && strcmp(locationSettingsValues[LocationSettingsValueIndex_Mls_Enabled], "true") == 0);
    bool mlsAgreementAccepted = locationSettingsValues[LocationSettingsValueIndex_Mls_AgreementAccepted] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_Mls_AgreementAccepted], "true") == 0;
    bool mlsOnlineEnabled = locationSettingsValues[LocationSettingsValueIndex_Mls_OnlineEnabled] != NULL
            && strcmp(locationSettingsValues[LocationSettingsValueIndex_Mls_OnlineEnabled], "true") == 0;
    bool hereEnabled = oldHereEnabled
            || (locationSettingsValues[LocationSettingsValueIndex_Here_Enabled] != NULL
                    && strcmp(locationSettingsValues[LocationSettingsValueIndex_Here_Enabled], "true") == 0);
    bool hereAgreementAccepted = oldHereAgreementAccepted
            || (locationSettingsValues[LocationSettingsValueIndex_Here_AgreementAccepted] != NULL
                    && strcmp(locationSettingsValues[LocationSettingsValueIndex_Here_AgreementAccepted], "true") == 0);
    // skip over here\online_enabled value.

    const int expectedCount = 23; // should equal: LocationSettingsKeys.split(';').count();
    for (int i = 0; i < expectedCount; ++i) {
        if (locationSettingsValues[i] != NULL) {
            free(locationSettingsValues[i]);
        }
    }
    free(locationSettingsValues);

    if (m_locationEnabled != locationEnabled) {
        m_locationEnabled = locationEnabled;
        emit q->locationEnabledChanged();
    }

    if (m_allowedDataSources != allowedDataSources) {
        m_allowedDataSources = allowedDataSources;
        emit q->allowedDataSourcesChanged();
    }

    if (m_gpsEnabled != gpsEnabled) {
        m_gpsEnabled = gpsEnabled;
        emit q->gpsEnabledChanged();
    }

    LocationSettings::OnlineAGpsState hereState = hereAgreementAccepted
            ? (hereEnabled ? LocationSettings::OnlineAGpsEnabled : LocationSettings::OnlineAGpsDisabled)
            : LocationSettings::OnlineAGpsAgreementNotAccepted;
    if (m_hereState != hereState) {
        m_hereState = hereState;
        emit q->hereStateChanged();
    }

    if (m_mlsEnabled != mlsEnabled) {
        m_mlsEnabled = mlsEnabled;
        emit q->mlsEnabledChanged();
    }

    LocationSettings::OnlineAGpsState mlsOnlineState = mlsAgreementAccepted
            ? ((mlsOnlineEnabled && m_mlsEnabled) ? LocationSettings::OnlineAGpsEnabled : LocationSettings::OnlineAGpsDisabled)
            : LocationSettings::OnlineAGpsAgreementNotAccepted;
    if (m_mlsOnlineState != mlsOnlineState) {
        m_mlsOnlineState = mlsOnlineState;
        emit q->mlsOnlineStateChanged();
    }

    if ((m_locationMode == LocationSettings::CustomMode) != customMode) {
        if (customMode) {
            m_locationMode = LocationSettings::CustomMode;
            emit q->locationModeChanged();
        } else {
            m_locationMode = calculateLocationMode();
            emit q->locationModeChanged();
        }
    }
}

void LocationSettingsPrivate::writeSettings()
{
    if (m_settingMultipleSettings) {
        return; // wait to write settings until all settings have been set.
    }

    // new file would be owned by creating process uid. we cannot allow this since the access is handled with group
    if (!QFile(LocationSettingsFile).exists()) {
        qWarning() << "Location settings configuration file does not exist. Refusing to create new.";
        return;
    }

    // set the aGPS providers key based upon the available providers
    QString agps_providers;
    if (m_mlsEnabled && m_hereState == LocationSettings::OnlineAGpsEnabled) {
        agps_providers = QStringLiteral("\"mls,here\"");
    } else if (m_mlsEnabled) {
        agps_providers = QStringLiteral("\"mls\"");
    } else if (m_hereState == LocationSettings::OnlineAGpsEnabled) {
        agps_providers = QStringLiteral("\"here\"");
    }

    QString locationSettingsValues;
    locationSettingsValues.append(boolToString(m_locationEnabled));
    locationSettingsValues.append(";");
    for (QMap<int, LocationSettings::DataSource>::const_iterator it = LocationSettingsValueIndexToDataSourceMap.constBegin();
            it != LocationSettingsValueIndexToDataSourceMap.constEnd(); it++) {
        locationSettingsValues.append(boolToString(m_allowedDataSources & it.value()));
        locationSettingsValues.append(";");
    }
    locationSettingsValues.append(boolToString(m_locationMode == LocationSettings::CustomMode));
    locationSettingsValues.append(";");
    locationSettingsValues.append(agps_providers);
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_gpsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_mlsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_mlsOnlineState != LocationSettings::OnlineAGpsAgreementNotAccepted));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_hereState == LocationSettings::OnlineAGpsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_hereState != LocationSettings::OnlineAGpsAgreementNotAccepted));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_hereState == LocationSettings::OnlineAGpsEnabled));
    // and the deprecated keys values...
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_mlsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_hereState == LocationSettings::OnlineAGpsEnabled));
    locationSettingsValues.append(";");
    locationSettingsValues.append(boolToString(m_hereState != LocationSettings::OnlineAGpsAgreementNotAccepted));

    if (!m_processMutex) {
        m_processMutex.reset(new Sailfish::KeyProvider::ProcessMutex(LocationSettingsFile.toLatin1().constData()));
    }

    m_processMutex->lock();
    if (SailfishKeyProvider_ini_write_multiple("/etc/location/",
                                               "/etc/location/location.conf",
                                               "location",
                                               LocationSettingsKeys.toLatin1().constData(),
                                               locationSettingsValues.toLatin1().constData(),
                                               ";") != 0) {
        qWarning() << "Unable to write location configuration settings!";
    }
    m_processMutex->unlock();
}
