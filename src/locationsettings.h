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

#ifndef LOCATIONSETTINGS_H
#define LOCATIONSETTINGS_H

#include <systemsettingsglobal.h>

#include <QObject>
#include <QString>

#define LOCATION_SETTINGS_LAST_DATA_SOURCE_BIT 31

class LocationSettingsPrivate;
class SYSTEMSETTINGS_EXPORT LocationSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool locationEnabled READ locationEnabled WRITE setLocationEnabled NOTIFY locationEnabledChanged)

    Q_PROPERTY(bool gpsEnabled READ gpsEnabled WRITE setGpsEnabled NOTIFY gpsEnabledChanged)
    Q_PROPERTY(bool gpsFlightMode READ gpsFlightMode WRITE setGpsFlightMode NOTIFY gpsFlightModeChanged)
    Q_PROPERTY(bool gpsAvailable READ gpsAvailable CONSTANT)

    Q_PROPERTY(OnlineAGpsState hereState READ hereState WRITE setHereState NOTIFY hereStateChanged)
    Q_PROPERTY(bool hereAvailable READ hereAvailable CONSTANT)

    Q_PROPERTY(bool mlsEnabled READ mlsEnabled WRITE setMlsEnabled NOTIFY mlsEnabledChanged)
    Q_PROPERTY(OnlineAGpsState mlsOnlineState READ mlsOnlineState WRITE setMlsOnlineState NOTIFY mlsOnlineStateChanged)
    Q_PROPERTY(bool mlsAvailable READ mlsAvailable CONSTANT)

    Q_PROPERTY(bool yandexLocatorEnabled READ yandexLocatorEnabled WRITE setYandexLocatorEnabled NOTIFY yandexLocatorEnabledChanged)
    Q_PROPERTY(OnlineAGpsState yandexLocatorOnlineState READ yandexLocatorOnlineState WRITE setYandexLocatorOnlineState NOTIFY yandexLocatorOnlineStateChanged)
    Q_PROPERTY(bool yandexLocatorAvailable READ yandexLocatorAvailable CONSTANT)

    Q_PROPERTY(LocationMode locationMode READ locationMode WRITE setLocationMode NOTIFY locationModeChanged)

    Q_PROPERTY(DataSources allowedDataSources READ allowedDataSources WRITE setAllowedDataSources NOTIFY allowedDataSourcesChanged)

    Q_ENUMS(OnlineAGpsState)
    Q_ENUMS(LocationMode)

public:
    enum Mode {
        AsynchronousMode,
        SynchronousMode
    };

    explicit LocationSettings(QObject *parent = 0);
    explicit LocationSettings(Mode mode, QObject *parent = 0);
    virtual ~LocationSettings();

    bool locationEnabled() const;
    void setLocationEnabled(bool enabled);

    bool gpsEnabled() const;
    void setGpsEnabled(bool enabled);
    bool gpsFlightMode() const;
    void setGpsFlightMode(bool flightMode);
    bool gpsAvailable() const;

    enum OnlineAGpsState {
        OnlineAGpsAgreementNotAccepted,
        OnlineAGpsDisabled,
        OnlineAGpsEnabled
    };

    OnlineAGpsState hereState() const;
    void setHereState(OnlineAGpsState state);
    bool hereAvailable() const;

    bool mlsEnabled() const;
    void setMlsEnabled(bool enabled);
    OnlineAGpsState mlsOnlineState() const;
    void setMlsOnlineState(OnlineAGpsState state);
    bool mlsAvailable() const;

    bool yandexLocatorEnabled() const;
    void setYandexLocatorEnabled(bool enabled);
    OnlineAGpsState yandexLocatorOnlineState() const;
    void setYandexLocatorOnlineState(OnlineAGpsState state);
    bool yandexLocatorAvailable() const;

    enum LocationMode {
        HighAccuracyMode,
        BatterySavingMode,
        DeviceOnlyMode,
        CustomMode
    };

    LocationMode locationMode() const;
    void setLocationMode(LocationMode locationMode);

    // Data sources are grouped roughly by type,
    // with gaps left for future expansion.
    enum DataSource {
        NoDataSources                   = 0UL,

        OnlineDataSources               = 1UL << 0,

        DeviceSensorsData               = 1UL << 5,
        BluetoothData                   = 1UL << 10,
        WlanData                        = 1UL << 15,
        CellTowerData                   = 1UL << 20,

        GpsData                         = 1UL << 25,
        GlonassData                     = 1UL << 26,
        BeidouData                      = 1UL << 27,
        GalileoData                     = 1UL << 28,
        QzssData                        = 1UL << 29,
        SbasData                        = 1UL << 30,

        LastDataSource                  = 1UL << LOCATION_SETTINGS_LAST_DATA_SOURCE_BIT
    };
    Q_DECLARE_FLAGS(DataSources, DataSource)
    Q_FLAG(DataSources)

    DataSources allowedDataSources() const;
    void setAllowedDataSources(DataSources dataSources);

signals:
    void hereStateChanged();
    void locationEnabledChanged();
    void gpsEnabledChanged();
    void gpsFlightModeChanged();
    void mlsEnabledChanged();
    void mlsOnlineStateChanged();
    void yandexLocatorEnabledChanged();
    void yandexLocatorOnlineStateChanged();
    void locationModeChanged();
    void allowedDataSourcesChanged();

private:
    LocationSettingsPrivate *d_ptr;
    Q_DISABLE_COPY(LocationSettings)
    Q_DECLARE_PRIVATE(LocationSettings)
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LocationSettings::DataSources)

#endif // LOCATIONSETTINGS_H
