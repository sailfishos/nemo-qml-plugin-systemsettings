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
#include <QTimer>
#include <QDebug>

#include <sailfishkeyprovider.h>
#include <sailfishkeyprovider_iniparser.h>
#include <sailfishkeyprovider_processmutex.h>

#include <networkmanager.h>
#include <networktechnology.h>

#include <limits>

namespace {
    const QString LocationSettingsDeprecatedCellIdPositioningEnabledKey = QStringLiteral("cell_id_positioning_enabled");
    const QString LocationSettingsDeprecatedHereEnabledKey = QStringLiteral("here_agreement_accepted");
    const QString LocationSettingsDeprecatedHereAgreementAcceptedKey = QStringLiteral("agreement_accepted");

    const QString PoweredPropertyName = QStringLiteral("Powered");
    const QString LocationSettingsDir = QStringLiteral("/etc/location/");
    const QString LocationSettingsFile = QStringLiteral("/etc/location/location.conf");
    const QString LocationSettingsSection = QStringLiteral("location");
    const QString LocationSettingsEnabledKey = QStringLiteral("enabled");
    const QString LocationSettingsCustomModeKey = QStringLiteral("custom_mode");
    const QString LocationSettingsGpsEnabledKey = QStringLiteral("gps\\enabled");

    const QString ProviderOfflineEnabledPattern = QStringLiteral("%1\\enabled");
    const QString ProviderAgreementAcceptedPattern = QStringLiteral("%1\\agreement_accepted");
    const QString ProviderOnlineEnabledPattern = QStringLiteral("%1\\online_enabled");

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

    const QString YandexName = QStringLiteral("yandex");
    const QString HereName = QStringLiteral("here");
    const QString MlsName = QStringLiteral("mls");
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
    , m_locationMode(LocationSettings::CustomMode)
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
    loadProviders();

    connect(&m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(readSettings()));
    connect(&m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(readSettings()));

    m_watcher.addPath(LocationSettingsDir);
    if (QFile(LocationSettingsFile).exists() && m_watcher.addPath(LocationSettingsFile)) {
        readSettings();
    } else {
        qWarning() << "Unable to follow location configuration file changes";
    }

    if (m_gpsTechInterface) {
        QDBusConnection::systemBus().connect("net.connman",
                                             "/net/connman/technology/gps",
                                             "net.connman.Technology",
                                             "PropertyChanged",
                                             this, SLOT(gpsTechPropertyChanged(QString, QDBusVariant)));
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

    delete m_gpsTechInterface;
    m_gpsTechInterface = 0;
}

void LocationSettingsPrivate::loadProviders()
{
    // for now just hard-coding the known potential providers.
    // can be replaced with config type of thing if there's need to support more and more providers.
    if (QFile::exists(QStringLiteral("/usr/libexec/geoclue-here"))) {
        LocationProvider provider;
        provider.hasAgreement = true;
        m_providers[HereName] = provider;
    }

    if (QFile::exists(QStringLiteral("/usr/libexec/geoclue-mlsdb"))) {
        LocationProvider provider;
        provider.hasAgreement = true;
        provider.offlineCapable = true;
        m_providers[MlsName] = provider;
    }

    if (QFile::exists(QStringLiteral("/usr/libexec/geoclue-yandex"))) {
        LocationProvider provider;
        provider.hasAgreement = true; // supposedly
        m_providers[YandexName] = provider;
    }
}

bool LocationSettingsPrivate::updateProvider(const QString &name, const LocationProvider &state)
{
    if (!m_providers.contains(name)) {
        return false;
    }

    LocationProvider &provider = m_providers[name];

    bool agreementChanged = false;
    if (provider.hasAgreement && provider.agreementAccepted != state.agreementAccepted) {
        agreementChanged = true;
        provider.agreementAccepted = state.agreementAccepted;
    }

    bool onlineEnabledChanged = false;
    if (provider.onlineCapable && provider.onlineEnabled != state.onlineEnabled) {
        onlineEnabledChanged = true;
        provider.onlineEnabled = state.onlineEnabled;
    }

    bool offlineEnabledChanged = false;
    if (provider.offlineCapable && provider.offlineEnabled != state.offlineEnabled) {
        offlineEnabledChanged = true;
        provider.offlineEnabled = state.offlineEnabled;
    }

    if (m_locationMode != LocationSettings::CustomMode) {
        if (m_locationMode == LocationSettings::DeviceOnlyMode) {
            // device only doesn't need any agreements
            if (m_pendingAgreements.contains(name)) {
                m_pendingAgreements.removeOne(name);
                emit q->pendingAgreementsChanged();
            }
        } else if (provider.hasAgreement) {
            if (!provider.agreementAccepted && !m_pendingAgreements.contains(name)) {
                m_pendingAgreements.append(name);
                emit q->pendingAgreementsChanged();
            } else if (provider.agreementAccepted && m_pendingAgreements.contains(name)) {
                m_pendingAgreements.removeOne(name);
                emit q->pendingAgreementsChanged();
            }
        }
    }

    if (offlineEnabledChanged && name == MlsName) {
        emit q->mlsEnabledChanged();
    }
    if (agreementChanged || onlineEnabledChanged) {
        if (name == HereName) {
            emit q->hereStateChanged();
        } else if (name == MlsName) {
            emit q->mlsOnlineStateChanged();
        } else if (name == YandexName) {
            emit q->yandexOnlineStateChanged();
        }
    }

    return true;
}

LocationSettings::OnlineAGpsState LocationSettingsPrivate::onlineState(const QString &name, bool *valid) const
{
    LocationSettings::OnlineAGpsState result;
    bool resultValid = true;

    if (!m_providers.contains(name)) {
        resultValid = false;
        result = LocationSettings::OnlineAGpsAgreementNotAccepted;
    } else {
        LocationProvider provider = m_providers.value(name);
        if (!provider.onlineCapable) {
            resultValid = false;
            result = LocationSettings::OnlineAGpsAgreementNotAccepted;
        } else if (!provider.agreementAccepted) {
            result = LocationSettings::OnlineAGpsAgreementNotAccepted;
        } else {
            result = provider.onlineEnabled ? LocationSettings::OnlineAGpsEnabled
                                            : LocationSettings::OnlineAGpsDisabled;
        }
    }

    if (valid) {
        *valid = resultValid;
    }

    return result;
}

void LocationSettingsPrivate::updateOnlineAgpsState(const QString &name, LocationSettings::OnlineAGpsState state)
{
    if (!m_providers.contains(name)) {
        return;
    }
    LocationProvider provider = m_providers.value(name);
    if (state == LocationSettings::OnlineAGpsAgreementNotAccepted) {
        provider.agreementAccepted = false;
    } else {
        provider.agreementAccepted = true;
        provider.onlineEnabled = (state == LocationSettings::OnlineAGpsEnabled);
    }
    updateProvider(name, provider);
    writeSettings();
}

void LocationSettingsPrivate::gpsTechPropertyChanged(const QString &propertyName, const QDBusVariant &)
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

static void checkOnlineStates(LocationSettings::OnlineAGpsState mode, bool *allOn, bool *allOff)
{
    if (mode == LocationSettings::OnlineAGpsEnabled) {
        *allOff = false;
    } else {
        *allOn = false;
    }
}

LocationSettings::LocationMode
LocationSettingsPrivate::calculateLocationMode() const
{
    bool allNetworkOn = true;
    bool allNetworkOff = true;

    bool networkLocationExists = false;
    bool allOfflineEnabled = true;

    for (const QString &name : m_providers.keys()) {
        bool valid = true;
        LocationSettings::OnlineAGpsState state = onlineState(name, &valid);
        if (valid) {
            networkLocationExists = true;
            checkOnlineStates(state, &allNetworkOn, &allNetworkOff);
        }

        LocationProvider provider = m_providers.value(name);
        if (provider.offlineCapable && !provider.offlineEnabled) {
            allOfflineEnabled = false;
        }
    }

    if (m_gpsEnabled && allNetworkOn && networkLocationExists && allOfflineEnabled) {
        return LocationSettings::HighAccuracyMode;
    } else if (!m_gpsEnabled && allNetworkOn && allOfflineEnabled) {
        return LocationSettings::BatterySavingMode;
    } else if (m_gpsEnabled && allNetworkOff && allOfflineEnabled) {
        return LocationSettings::DeviceOnlyMode;
    }

    return LocationSettings::CustomMode;
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

QStringList LocationSettings::locationProviders() const
{
    Q_D(const LocationSettings);
    return d->m_providers.keys();
}

LocationProvider LocationSettings::providerInfo(const QString &name) const
{
    Q_D(const LocationSettings);
    return d->m_providers.value(name.toLower());
}

bool LocationSettings::updateLocationProvider(const QString &name, const LocationProvider &providerState)
{
    Q_D(LocationSettings);
    if (!d->updateProvider(name.toLower(), providerState)) {
        return false;
    }

    d->writeSettings();
    return true;
}

/*Mozilla Location services*/
bool LocationSettings::mlsEnabled() const
{
    Q_D(const LocationSettings);
    return d->m_providers.value(MlsName).offlineEnabled;
}

void LocationSettings::setMlsEnabled(bool enabled)
{
    if (mlsAvailable() && enabled != mlsEnabled()) {
        LocationProvider provider = providerInfo(MlsName);
        provider.offlineEnabled = enabled;
        updateLocationProvider(MlsName, provider);
    }
}

LocationSettings::OnlineAGpsState LocationSettings::mlsOnlineState() const
{
    Q_D(const LocationSettings);
    return d->onlineState(MlsName);
}

void LocationSettings::setMlsOnlineState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    d->updateOnlineAgpsState(MlsName, state);
}

bool LocationSettings::mlsAvailable() const
{
    Q_D(const LocationSettings);
    return d->m_providers.contains(MlsName);
}

/*Yandex  services*/
LocationSettings::OnlineAGpsState LocationSettings::yandexOnlineState() const
{
    Q_D(const LocationSettings);
    return d->onlineState(YandexName);
}

void LocationSettings::setYandexOnlineState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    d->updateOnlineAgpsState(YandexName, state);
}

bool LocationSettings::yandexAvailable() const
{
    Q_D(const LocationSettings);
    return d->m_providers.contains(YandexName);
}

/*HERE*/
LocationSettings::OnlineAGpsState LocationSettings::hereState() const
{
    Q_D(const LocationSettings);
    return d->onlineState(HereName);
}

void LocationSettings::setHereState(LocationSettings::OnlineAGpsState state)
{
    Q_D(LocationSettings);
    d->updateOnlineAgpsState(HereName, state);
}

bool LocationSettings::hereAvailable() const
{
    Q_D(const LocationSettings);
    return d->m_providers.contains(HereName);
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

    d->m_settingMultipleSettings = true;
    d->m_locationMode = locationMode;

    if (locationMode != CustomMode) {
        setGpsEnabled(locationMode == HighAccuracyMode || locationMode == DeviceOnlyMode);
        bool enableOnline = (locationMode != DeviceOnlyMode);

        for (const QString &name : d->m_providers.keys()) {
            LocationProvider provider = d->m_providers.value(name);
            provider.offlineEnabled = true;
            provider.onlineEnabled = enableOnline;
            d->updateProvider(name, provider);
        }
    } else if (d->m_pendingAgreements.isEmpty()) {
        d->m_pendingAgreements.clear();
        emit pendingAgreementsChanged();
    }

    d->m_settingMultipleSettings = false;
    d->writeSettings();
    emit locationModeChanged();
}

QStringList LocationSettings::pendingAgreements() const
{
    Q_D(const LocationSettings);
    return d->m_pendingAgreements;
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
    bool locationEnabled = false;
    bool customMode = false;
    bool gpsEnabled = false;

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
        bool oldMlsEnabled = false;
        bool oldHereEnabled = false;
        bool oldHereAgreementAccepted = false;
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedCellIdPositioningEnabledKey, &oldMlsEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedHereEnabledKey, &oldHereEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsDeprecatedHereAgreementAcceptedKey, &oldHereAgreementAccepted);

        // then read the current keys
        ini.readBool(LocationSettingsSection, LocationSettingsEnabledKey, &locationEnabled);
        ini.readBool(LocationSettingsSection, LocationSettingsCustomModeKey, &customMode);
        ini.readBool(LocationSettingsSection, LocationSettingsGpsEnabledKey, &gpsEnabled);

        for (const QString &name : m_providers.keys()) {
            LocationProvider provider;
            if (name == MlsName) {
                provider.offlineEnabled = oldMlsEnabled;
            } else if (name == HereName) {
                provider.onlineEnabled = oldHereEnabled;
                provider.agreementAccepted = oldHereAgreementAccepted;
            }
            ini.readBool(LocationSettingsSection, ProviderOfflineEnabledPattern.arg(name), &provider.offlineEnabled);
            ini.readBool(LocationSettingsSection, ProviderOnlineEnabledPattern.arg(name), &provider.onlineEnabled);
            ini.readBool(LocationSettingsSection, ProviderAgreementAcceptedPattern.arg(name), &provider.agreementAccepted);
            updateProvider(name, provider);
        }

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

    if ((m_locationMode == LocationSettings::CustomMode) != customMode) {
        if (customMode) {
            m_locationMode = LocationSettings::CustomMode;
            emit q->locationModeChanged();
            if (!m_pendingAgreements.isEmpty()) {
                m_pendingAgreements.clear();
                emit q->pendingAgreementsChanged();
            }
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

    // write the values to the conf file
    {
        IniFile ini(LocationSettingsFile);

        ini.writeBool(LocationSettingsSection, LocationSettingsEnabledKey, m_locationEnabled);
        ini.writeBool(LocationSettingsSection, LocationSettingsCustomModeKey, m_locationMode == LocationSettings::CustomMode);
        ini.writeBool(LocationSettingsSection, LocationSettingsGpsEnabledKey, m_gpsEnabled);

        for (const QString &name : m_providers.keys()) {
            LocationProvider provider = m_providers.value(name);
            if (provider.offlineCapable) {
                ini.writeBool(LocationSettingsSection, ProviderOfflineEnabledPattern.arg(name), provider.offlineEnabled);
            }
            if (provider.onlineCapable) {
                ini.writeBool(LocationSettingsSection, ProviderOnlineEnabledPattern.arg(name), provider.onlineEnabled);
            }
            if (provider.hasAgreement) {
                ini.writeBool(LocationSettingsSection, ProviderAgreementAcceptedPattern.arg(name), provider.agreementAccepted);
            }
        }

        // write the MDM allowed allowed data source keys
        for (QMap<LocationSettings::DataSource, QString>::const_iterator
                it = AllowedDataSourcesKeys.constBegin();
                it != AllowedDataSourcesKeys.constEnd();
                it++) {
            ini.writeBool(LocationSettingsSection, it.value(), m_allowedDataSources & it.key());
        }
    }
}
