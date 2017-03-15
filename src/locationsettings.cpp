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

#include <QFile>
#include <QSettings>
#include <QDebug>
#include <QStringList>

#include <sailfishkeyprovider.h>
#include <sailfishkeyprovider_iniparser.h>
#include <sailfishkeyprovider_processmutex.h>

namespace {
const QString LocationSettingsDir = QStringLiteral("/etc/location/");
const QString LocationSettingsFile = QStringLiteral("/etc/location/location.conf");
const QString LocationSettingsKeys = QStringLiteral(
                                        "enabled"                     ";"
                                        "agps_providers"              ";"
                                        "gps\\enabled"                ";"
                                        "gps\\flightMode"             ";"
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
}

LocationSettings::LocationSettings(QObject *parent)
    :   QObject(parent)
    , m_locationEnabled(false)
    , m_gpsEnabled(false)
    , m_mlsEnabled(true)
    , m_mlsOnlineState(OnlineAGpsAgreementNotAccepted)
    , m_hereState(OnlineAGpsAgreementNotAccepted)
{
    connect(&m_watcher, SIGNAL(fileChanged(QString)), this, SLOT(readSettings()));
    connect(&m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(readSettings()));

    m_watcher.addPath(LocationSettingsDir);
    if (QFile(LocationSettingsFile).exists() && m_watcher.addPath(LocationSettingsFile)) {
        readSettings();
    } else {
        qWarning() << "Unable to follow location configuration file changes";
    }
}

bool LocationSettings::locationEnabled() const
{
    return m_locationEnabled;
}

void LocationSettings::setLocationEnabled(bool enabled)
{
    if (enabled != m_locationEnabled) {
        m_locationEnabled = enabled;
        writeSettings();
        emit locationEnabledChanged();
    }
}

bool LocationSettings::gpsEnabled() const
{
    return m_gpsEnabled;
}

void LocationSettings::setGpsEnabled(bool enabled)
{
    if (enabled != m_gpsEnabled) {
        m_gpsEnabled = enabled;
        writeSettings();
        emit gpsEnabledChanged();
    }
}

bool LocationSettings::gpsFlightMode() const
{
    return m_gpsFlightMode;
}

void LocationSettings::setGpsFlightMode(bool flightMode)
{
    if (flightMode != m_gpsFlightMode) {
        m_gpsFlightMode = flightMode;
        writeSettings();
        emit gpsFlightModeChanged();
    }
}

bool LocationSettings::gpsAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-hybris"));
}

bool LocationSettings::mlsEnabled() const
{
    return m_mlsEnabled;
}

void LocationSettings::setMlsEnabled(bool enabled)
{
    if (enabled != m_mlsEnabled) {
        m_mlsEnabled = enabled;
        writeSettings();
        emit mlsEnabledChanged();
    }
}

LocationSettings::OnlineAGpsState LocationSettings::mlsOnlineState() const
{
    return m_mlsOnlineState;
}

void LocationSettings::setMlsOnlineState(LocationSettings::OnlineAGpsState state)
{
    if (state == m_mlsOnlineState)
        return;

    m_mlsOnlineState = state;
    writeSettings();
    emit mlsOnlineStateChanged();
}

bool LocationSettings::mlsAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-mlsdb"));
}

LocationSettings::OnlineAGpsState LocationSettings::hereState() const
{
    return m_hereState;
}

void LocationSettings::setHereState(LocationSettings::OnlineAGpsState state)
{
    if (state == m_hereState)
        return;

    m_hereState = state;
    writeSettings();
    emit hereStateChanged();
}

bool LocationSettings::hereAvailable() const
{
    return QFile::exists(QStringLiteral("/usr/libexec/geoclue-here"));
}

void LocationSettings::readSettings()
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
    bool oldMlsEnabled = locationSettingsValues[10]!= NULL && strcmp(locationSettingsValues[10], "true") == 0;
    bool oldHereEnabled = locationSettingsValues[11] != NULL && strcmp(locationSettingsValues[11], "true") == 0;
    bool oldHereAgreementAccepted = locationSettingsValues[12] != NULL && strcmp(locationSettingsValues[12], "true") == 0;
    // then read the new key values (overriding with deprecated values if needed):
    bool locationEnabled = locationSettingsValues[0] != NULL && strcmp(locationSettingsValues[0], "true") == 0;
    // skip over the agps_providers value at [1]
    bool gpsEnabled = locationSettingsValues[2]!= NULL && strcmp(locationSettingsValues[2], "true") == 0;
    bool gpsFlightMode = locationSettingsValues[3]!= NULL && strcmp(locationSettingsValues[3], "true") == 0; // TODO INDEXES ETC
    bool mlsEnabled = oldMlsEnabled || (locationSettingsValues[4] != NULL && strcmp(locationSettingsValues[4], "true") == 0);
    bool mlsAgreementAccepted = locationSettingsValues[5] != NULL && strcmp(locationSettingsValues[5], "true") == 0;
    bool mlsOnlineEnabled = locationSettingsValues[6] != NULL && strcmp(locationSettingsValues[6], "true") == 0;
    bool hereEnabled = oldHereEnabled || (locationSettingsValues[7] != NULL && strcmp(locationSettingsValues[7], "true") == 0);
    bool hereAgreementAccepted = oldHereAgreementAccepted || (locationSettingsValues[8] != NULL && strcmp(locationSettingsValues[8], "true") == 0);
    // skip over here\online_enabled value at [9]

    const int expectedCount = 13; // should equal: LocationSettingsKeys.split(';').count();
    for (int i = 0; i < expectedCount; ++i) {
        if (locationSettingsValues[i] != NULL) {
            free(locationSettingsValues[i]);
        }
    }
    free(locationSettingsValues);

    if (m_locationEnabled != locationEnabled) {
        m_locationEnabled = locationEnabled;
        emit locationEnabledChanged();
    }

    if (m_gpsEnabled != gpsEnabled) {
        m_gpsEnabled = gpsEnabled;
        emit gpsEnabledChanged();
    }

    if (m_gpsFlightMode != gpsFlightMode) {
        m_gpsFlightMode = gpsFlightMode;
        emit gpsFlightModeChanged();
    }

    OnlineAGpsState hereState = hereAgreementAccepted ? (hereEnabled ? OnlineAGpsEnabled : OnlineAGpsDisabled) : OnlineAGpsAgreementNotAccepted;
    if (m_hereState != hereState) {
        m_hereState = hereState;
        emit hereStateChanged();
    }

    if (m_mlsEnabled != mlsEnabled) {
        m_mlsEnabled = mlsEnabled;
        emit mlsEnabledChanged();
    }

    OnlineAGpsState mlsOnlineState = mlsAgreementAccepted ? ((mlsOnlineEnabled && m_mlsEnabled) ? OnlineAGpsEnabled : OnlineAGpsDisabled) : OnlineAGpsAgreementNotAccepted;
    if (m_mlsOnlineState != mlsOnlineState) {
        m_mlsOnlineState = mlsOnlineState;
        emit mlsOnlineStateChanged();
    }
}

void LocationSettings::writeSettings()
{
    // new file would be owned by creating process uid. we cannot allow this since the access is handled with group
    if (!QFile(LocationSettingsFile).exists()) {
        qWarning() << "Location settings configuration file does not exist. Refusing to create new.";
        return;
    }

    // set the aGPS providers key based upon the available providers
    QString agps_providers;
    if (m_mlsEnabled && m_hereState == OnlineAGpsEnabled) {
        agps_providers = QStringLiteral("\"mls,here\"");
    } else if (m_mlsEnabled) {
        agps_providers = QStringLiteral("\"mls\"");
    } else if (m_hereState == OnlineAGpsEnabled) {
        agps_providers = QStringLiteral("\"here\"");
    }

    QString locationSettingsValues;
    locationSettingsValues.append(m_locationEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(agps_providers);
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_gpsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_gpsFlightMode ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_mlsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_mlsOnlineState != OnlineAGpsAgreementNotAccepted ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_mlsOnlineState == OnlineAGpsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_hereState == OnlineAGpsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_hereState != OnlineAGpsAgreementNotAccepted ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_hereState == OnlineAGpsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    // and the deprecated keys values...
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_mlsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_hereState == OnlineAGpsEnabled ? QStringLiteral("true") : QStringLiteral("false"));
    locationSettingsValues.append(";");
    locationSettingsValues.append(m_hereState != OnlineAGpsAgreementNotAccepted ? QStringLiteral("true") : QStringLiteral("false"));

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
