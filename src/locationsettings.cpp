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
#include <QDebug>
#include <QStringList>

#include <sailfishkeyprovider.h>
#include <sailfishkeyprovider_iniparser.h>
#include <sailfishkeyprovider_processmutex.h>

#include <networkmanager.h>
#include <networktechnology.h>

namespace {
QString boolToString(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }
const QString LocationSettingsDir = QStringLiteral("/etc/location/");
const QString LocationSettingsFile = QStringLiteral("/etc/location/location.conf");
const QString LocationSettingsKeys = QStringLiteral(
                                        "enabled"                     ";"
                                        "agps_providers"              ";"
                                        "gps\\enabled"                ";"
                                        "mls\\enabled"                ";"
                                        "mls\\agreement_accepted"     ";"
                                        "mls\\online_enabled"         ";"
                                        "here\\enabled"               ";"
                                        "here\\agreement_accepted"    ";"
                                        "here\\online_enabled"        ";"
                                        /* and the deprecated keys: */
                                        "cell_id_positioning_enabled" ";"
                                        "here_agreement_accepted"     ";"
                                        "agreement_accepted");
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
    , m_connMan(Q_NULLPTR)
    , m_gpsTech(Q_NULLPTR)
    , m_gpsTechInterface(mode == LocationSettings::AsynchronousMode
                         ? Q_NULLPTR
                         : new QDBusInterface("net.connman",
                                              "/net/connman/technology/gps",
                                              "net.connman.Technology",
                                              QDBusConnection::systemBus()))
{

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
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-mlsdb"));
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
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-here"));
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
    bool oldMlsEnabled = locationSettingsValues[9] != NULL && strcmp(locationSettingsValues[9], "true") == 0;
    bool oldHereEnabled = locationSettingsValues[10] != NULL && strcmp(locationSettingsValues[10], "true") == 0;
    bool oldHereAgreementAccepted = locationSettingsValues[11] != NULL && strcmp(locationSettingsValues[11], "true") == 0;
    // then read the new key values (overriding with deprecated values if needed):
    bool locationEnabled = locationSettingsValues[0] != NULL && strcmp(locationSettingsValues[0], "true") == 0;
    // skip over the agps_providers value at [1]
    bool gpsEnabled = locationSettingsValues[2] != NULL && strcmp(locationSettingsValues[2], "true") == 0;
    bool mlsEnabled = oldMlsEnabled || (locationSettingsValues[3] != NULL && strcmp(locationSettingsValues[3], "true") == 0);
    bool mlsAgreementAccepted = locationSettingsValues[4] != NULL && strcmp(locationSettingsValues[4], "true") == 0;
    bool mlsOnlineEnabled = locationSettingsValues[5] != NULL && strcmp(locationSettingsValues[5], "true") == 0;
    bool hereEnabled = oldHereEnabled || (locationSettingsValues[6] != NULL && strcmp(locationSettingsValues[6], "true") == 0);
    bool hereAgreementAccepted = oldHereAgreementAccepted || (locationSettingsValues[7] != NULL && strcmp(locationSettingsValues[7], "true") == 0);
    // skip over here\online_enabled value at [8]

    const int expectedCount = 12; // should equal: LocationSettingsKeys.split(';').count();
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
}

void LocationSettingsPrivate::writeSettings()
{
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
