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
    const QString PoweredPropertyName = QStringLiteral("Powered");
    const QString LocationSettingsDir = QStringLiteral("/etc/location/");
    const QString LocationSettingsFile = QStringLiteral("/etc/location/location.conf");
    const QString LocationSettingsSection = QStringLiteral("location");
    const QString LocationSettingsDeprecatedCellIdPositioningEnabledKey = QStringLiteral("cell_id_positioning_enabled");
    const QString LocationSettingsDeprecatedHereEnabledKey = QStringLiteral("here_agreement_accepted");
    const QString LocationSettingsDeprecatedHereAgreementAcceptedKey = QStringLiteral("agreement_accepted");
    const QString LocationSettingsEnabledKey = QStringLiteral("enabled");
    const QString LocationSettingsCustomModeKey = QStringLiteral("custom_mode");
    const QString LocationSettingsAgpsProvidersKey = QStringLiteral("agps_providers");
    const QString LocationSettingsGpsEnabledKey = QStringLiteral("gps\\enabled");
    const QString LocationSettingsMlsEnabledKey = QStringLiteral("mls\\enabled");
    const QString LocationSettingsMlsAgreementAcceptedKey = QStringLiteral("mls\\agreement_accepted");
    const QString LocationSettingsMlsOnlineEnabledKey = QStringLiteral("mls\\online_enabled");
    const QString LocationSettingsHereEnabledKey = QStringLiteral("here\\enabled");
    const QString LocationSettingsHereAgreementAcceptedKey = QStringLiteral("here\\agreement_accepted");
    const QString LocationSettingsHereOnlineEnabledKey = QStringLiteral("here\\online_enabled");
    const QString LocationSettingsYandexLocatorEnabledKey = QStringLiteral("yandex\\enabled");
    const QString LocationSettingsYandexLocatorAgreementAcceptedKey = QStringLiteral("yandex\\agreement_accepted");
    const QString LocationSettingsYandexLocatorOnlineEnabledKey = QStringLiteral("yandex\\online_enabled");
    const QMap<LocationSettings::DataSource, QString> AllowedDataSourcesKeys {
        { LocationSettings::OnlineDataSources, QStringLiteral("allowed_data_sources\\online") },
        { LocationSettings::DeviceSensorsData, QStringLiteral("allowed_data_sources\\device_sensors") },
        { LocationSettings::BluetoothData, QStringLiteral("allowed_data_sources\\bt_addr") },
        { LocationSettings::WlanData, QStringLiteral("allowed_data_sources\\wlan_data") },
        { LocationSettings::CellTowerData, QStringLiteral("allowed_data_sources\\cell_data") },
        { LocationSettings::GpsData, QStringLiteral("allowed_data_sources\\gps") },
        { LocationSettings::GlonassData, QStringLiteral("allowed_data_sources\\glonass") },
        { LocationSettings::BeidouData, QStringLiteral("allowed_data_sources\\beidou") },
        { LocationSettings::GalileoData, QStringLiteral("allowed_data_sources\\galileo") },
        { LocationSettings::QzssData, QStringLiteral("allowed_data_sources\\qzss") },
        { LocationSettings::SbasData, QStringLiteral("allowed_data_sources\\sbas") }
    };
}

IniFile::IniFile(const QString &fileName)
    : m_fileName(fileName)
    , m_keyFile(Q_NULLPTR)
    , m_error(Q_NULLPTR)
    , m_modified(false)
    , m_valid(false)
{
    m_processMutex.reset(new Sailfish::KeyProvider::ProcessMutex(qPrintable(m_fileName)));
    m_processMutex->lock();
    m_keyFile = g_key_file_new();
    if (!m_keyFile) {
        qWarning() << "Unable to allocate key file:" << m_fileName;
    } else {
        g_key_file_load_from_file(m_keyFile,
                                  qPrintable(m_fileName),
                                  G_KEY_FILE_NONE,
                                  &m_error);
        if (m_error) {
            qWarning() << "Unable to load key file:" << m_fileName << ":"
                       << m_error->code << QString::fromUtf8(m_error->message);
            g_error_free(m_error);
            m_error = Q_NULLPTR;
        } else {
            m_valid = true;
        }
    }
}

IniFile::~IniFile()
{
    if (m_valid && m_modified) {
        g_key_file_save_to_file(m_keyFile,
                                qPrintable(m_fileName),
                                &m_error);
        if (m_error) {
            qWarning() << "Unable to save changes to key file:" << m_fileName << ":"
                       << m_error->code << QString::fromUtf8(m_error->message);
            g_error_free(m_error);
            m_error = Q_NULLPTR;
        }
    }
    if (m_keyFile) {
        g_key_file_free(m_keyFile);
    }
    m_processMutex->unlock();
}

bool IniFile::isValid() const
{
    return m_valid;
}

bool IniFile::readBool(const QString &section, const QString &key, bool *value, bool defaultValue)
{
    gboolean val = g_key_file_get_boolean(m_keyFile,
                                          qPrintable(section),
                                          qPrintable(key),
                                          &m_error);
    if (m_error) {
        // if MDM hasn't set allowed / disallowed data sources yet, the key may not exist.
        if (m_error->code != G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
            // other errors should be printed to the journal.
            qWarning() << "Unable to read bool from key file:" << m_fileName << ":"
                       << section << "/" << key << ":"
                       << m_error->code << QString::fromUtf8(m_error->message);
        }
        g_error_free(m_error);
        m_error = Q_NULLPTR;
        *value = defaultValue;
        return false;
    }
    *value = val;
    return true;
}

void IniFile::writeBool(const QString &section, const QString &key, bool value)
{
    g_key_file_set_boolean(m_keyFile,
                           qPrintable(section),
                           qPrintable(key),
                           value ? TRUE : FALSE);
    m_modified = true;
}

void IniFile::writeString(const QString &section, const QString &key, const QString &value)
{
    g_key_file_set_string(m_keyFile,
                          qPrintable(section),
                          qPrintable(key),
                          qPrintable(value));
    m_modified = true;
}

LocationSettingsPrivate::LocationSettingsPrivate(LocationSettings::Mode mode, LocationSettings *settings)
    : QObject(settings)
    , q(settings)
    , m_locationEnabled(false)
    , m_gpsEnabled(false)
    , m_mlsEnabled(true)
    , m_yandexLocatorEnabled(true)
    , m_mlsOnlineState(LocationSettings::OnlineAGpsAgreementNotAccepted)
    , m_yandexLocatorOnlineState(LocationSettings::OnlineAGpsAgreementNotAccepted)
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
    connect(q, &LocationSettings::yandexLocatorEnabledChanged,
            this, &LocationSettingsPrivate::recalculateLocationMode);
    connect(q, &LocationSettings::yandexLocatorOnlineStateChanged,
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
    bool nls = m_mlsEnabled || m_yandexLocatorEnabled;
    bool nls_available = mlsAvailable() || yandexLocatorAvailable();

    if (m_gpsEnabled
            && (!nls_available ||
                    (nls && (m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled || m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsEnabled)))
            && (!hereAvailable() || m_hereState == LocationSettings::OnlineAGpsEnabled)) {
        return LocationSettings::HighAccuracyMode;
    } else if (!m_gpsEnabled
            && (!nls_available ||
                    (nls &&
                        ((m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled || m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsEnabled )
                        || (m_mlsOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted || m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted))))
            && (!hereAvailable() ||
                    (m_hereState == LocationSettings::OnlineAGpsEnabled
                    || m_hereState == LocationSettings::OnlineAGpsAgreementNotAccepted))) {
        return LocationSettings::BatterySavingMode;
    } else if (m_gpsEnabled
            && (!nls_available ||
                    (nls &&
                        ((m_mlsOnlineState == LocationSettings::OnlineAGpsDisabled || m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsDisabled)
                        ||(m_mlsOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted || m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsAgreementNotAccepted))))
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

bool LocationSettingsPrivate::yandexLocatorAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-yandex"));
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

/*Mozilla Location services*/

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

/*Yandex locator services*/

bool LocationSettings::yandexLocatorEnabled() const
{
    Q_D(const LocationSettings);
    return d->m_yandexLocatorEnabled;
}

void LocationSettings::setYandexLocatorEnabled(bool enabled)
{
    Q_D(LocationSettings);
    if (enabled != d->m_yandexLocatorEnabled) {
        d->m_yandexLocatorEnabled = enabled;
        d->writeSettings();
        emit yandexLocatorEnabledChanged();
    }
}

LocationSettings::OnlineAGpsState LocationSettings::yandexLocatorOnlineState() const
{
    Q_D(const LocationSettings);
    return d->m_yandexLocatorOnlineState;
}

void LocationSettings::setYandexLocatorOnlineState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    if (state == d->m_yandexLocatorOnlineState)
        return;

    d->m_yandexLocatorOnlineState = state;
    d->writeSettings();
    emit yandexLocatorOnlineStateChanged();
}

bool LocationSettings::yandexLocatorAvailable() const
{
    Q_D(const LocationSettings);
    return d->yandexLocatorAvailable();
}

/*HERE*/
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
        if (yandexLocatorAvailable()) {
            setYandexLocatorEnabled(true);
            if (yandexLocatorOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setYandexLocatorOnlineState(LocationSettings::OnlineAGpsEnabled);
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
        if (yandexLocatorAvailable()) {
            setYandexLocatorEnabled(true);
            if (yandexLocatorOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setYandexLocatorOnlineState(LocationSettings::OnlineAGpsEnabled);
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
        if (yandexLocatorAvailable()) {
            setYandexLocatorEnabled(true);
            if (yandexLocatorOnlineState() != LocationSettings::OnlineAGpsAgreementNotAccepted) {
                setYandexLocatorOnlineState(LocationSettings::OnlineAGpsDisabled);
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
    // deprecated key values
    bool oldMlsEnabled = false;
    bool oldHereEnabled = false;
    bool oldHereAgreementAccepted = false;
    bool oldYandexLocatorEnabled = false;

    // current key values
    bool locationEnabled = false;
    bool customMode = false;
    bool gpsEnabled = false;
    bool mlsEnabled = false;
    bool mlsAgreementAccepted = false;
    bool mlsOnlineEnabled = false;
    bool yandexLocatorEnabled = false;
    bool yandexLocatorAgreementAccepted = false;
    bool yandexLocatorOnlineEnabled = false;

    bool hereEnabled = false;
    bool hereAgreementAccepted = false;

    // MDM allowed data source key values
    LocationSettings::DataSources allowedDataSources = static_cast<LocationSettings::DataSources>(std::numeric_limits<quint32>::max());

    // Read values from the settings file.  Scope ensures process mutex locking.
    {
        IniFile ini(LocationSettingsFile);
        if (!ini.isValid()) {
            qWarning() << "Unable to read location configuration settings!";
            return;
        }

        // read the deprecated keys first for backward compatibility
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedCellIdPositioningEnabledKey, &oldMlsEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedHereEnabledKey, &oldHereEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedHereAgreementAcceptedKey, &oldHereAgreementAccepted);

        // then read the current keys
        ini.readBool(LocationSettingsSection, LocationSettingsEnabledKey, &locationEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsCustomModeKey, &customMode);
        ini.readBool(LocationSettingsSection, LocationSettingsGpsEnabledKey, &gpsEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsMlsEnabledKey, &mlsEnabled, oldMlsEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsMlsAgreementAcceptedKey, &mlsAgreementAccepted);
        ini.readBool(LocationSettingsSection, LocationSettingsMlsOnlineEnabledKey, &mlsOnlineEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsYandexLocatorEnabledKey, &yandexLocatorEnabled, oldYandexLocatorEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsYandexLocatorAgreementAcceptedKey, &yandexLocatorAgreementAccepted);
        ini.readBool(LocationSettingsSection, LocationSettingsYandexLocatorOnlineEnabledKey, &yandexLocatorOnlineEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsHereEnabledKey, &hereEnabled, oldHereEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsHereAgreementAcceptedKey, &hereAgreementAccepted, oldHereAgreementAccepted);

        // read the MDM allowed allowed data source keys
        bool dataSourceAllowed = true;
        for (QMap<LocationSettings::DataSource, QString>::const_iterator
                it = AllowedDataSourcesKeys.constBegin();
                it != AllowedDataSourcesKeys.constEnd();
                it++) {
            ini.readBool(LocationSettingsSection, it.value(), &dataSourceAllowed, true);
            if (!dataSourceAllowed) {
                allowedDataSources &= ~it.key();
            }
        }
    }

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

    if (m_yandexLocatorEnabled != yandexLocatorEnabled) {
        m_yandexLocatorEnabled = yandexLocatorEnabled;
        emit q->yandexLocatorEnabledChanged();
    }

    LocationSettings::OnlineAGpsState mlsOnlineState = mlsAgreementAccepted
            ? ((mlsOnlineEnabled && m_mlsEnabled) ? LocationSettings::OnlineAGpsEnabled : LocationSettings::OnlineAGpsDisabled)
            : LocationSettings::OnlineAGpsAgreementNotAccepted;
    if (m_mlsOnlineState != mlsOnlineState) {
        m_mlsOnlineState = mlsOnlineState;
        emit q->mlsOnlineStateChanged();
    }

    LocationSettings::OnlineAGpsState yandexLocatorOnlineState = yandexLocatorAgreementAccepted
            ? ((yandexLocatorOnlineEnabled && m_yandexLocatorEnabled) ? LocationSettings::OnlineAGpsEnabled : LocationSettings::OnlineAGpsDisabled)
            : LocationSettings::OnlineAGpsAgreementNotAccepted;
    if (m_yandexLocatorOnlineState != yandexLocatorOnlineState) {
        m_yandexLocatorOnlineState = yandexLocatorOnlineState;
        emit q->yandexLocatorOnlineStateChanged();
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

    // set the available location providers value based upon the enabled providers
    QStringList agps_providers;

    if (m_mlsEnabled) {
        agps_providers.append("mls");
    }
    if (m_hereState == LocationSettings::OnlineAGpsEnabled) {
        agps_providers.append("here");
    }
    if (m_yandexLocatorEnabled) {
        agps_providers.append("yandex");
    }

    // write the values to the conf file
    {
        IniFile ini(LocationSettingsFile);

        // write deprecated keys for backward compatibility
        ini.writeBool(LocationSettingsSection, LocationSettingsDeprecatedCellIdPositioningEnabledKey, m_mlsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsDeprecatedHereEnabledKey, m_hereState == LocationSettings::OnlineAGpsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsDeprecatedHereAgreementAcceptedKey, m_hereState != LocationSettings::OnlineAGpsAgreementNotAccepted);

        // then write the current keys
        ini.writeBool(LocationSettingsSection, LocationSettingsEnabledKey, m_locationEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsCustomModeKey, m_locationMode == LocationSettings::CustomMode);
        ini.writeBool(LocationSettingsSection, LocationSettingsGpsEnabledKey, m_gpsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsMlsEnabledKey, m_mlsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsMlsAgreementAcceptedKey, m_mlsOnlineState != LocationSettings::OnlineAGpsAgreementNotAccepted);
        ini.writeBool(LocationSettingsSection, LocationSettingsMlsOnlineEnabledKey, m_mlsOnlineState == LocationSettings::OnlineAGpsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsYandexLocatorEnabledKey, m_yandexLocatorEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsYandexLocatorAgreementAcceptedKey, m_yandexLocatorOnlineState != LocationSettings::OnlineAGpsAgreementNotAccepted);
        ini.writeBool(LocationSettingsSection, LocationSettingsYandexLocatorOnlineEnabledKey, m_yandexLocatorOnlineState == LocationSettings::OnlineAGpsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsHereEnabledKey, m_hereState == LocationSettings::OnlineAGpsEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsHereAgreementAcceptedKey, m_hereState != LocationSettings::OnlineAGpsAgreementNotAccepted);
        ini.writeBool(LocationSettingsSection, LocationSettingsHereOnlineEnabledKey, m_hereState == LocationSettings::OnlineAGpsEnabled);

        // write the available location providers based on the enabled plugins
        ini.writeString(LocationSettingsSection, LocationSettingsAgpsProvidersKey, "\""+agps_providers.join(",")+"\"");

        // write the MDM allowed allowed data source keys
        for (QMap<LocationSettings::DataSource, QString>::const_iterator
                it = AllowedDataSourcesKeys.constBegin();
                it != AllowedDataSourcesKeys.constEnd();
                it++) {
            ini.writeBool(LocationSettingsSection, it.value(), m_allowedDataSources & it.key());
        }
    }
}
