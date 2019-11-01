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

#ifndef NEMO_SYSTEMSETTINGS_LOCATIONSETTINGS_P_H
#define NEMO_SYSTEMSETTINGS_LOCATIONSETTINGS_P_H

#include <QFileSystemWatcher>
#include <QScopedPointer>
#include <QDBusInterface>
#include <QVariant>
#include <QString>

#include <sailfishkeyprovider_processmutex.h>

#include <glib.h>

#include "locationsettings.h"

class NetworkManager;
class NetworkTechnology;

class LocationSettingsPrivate : public QObject
{
    Q_OBJECT
    friend class LocationSettings;
    LocationSettings *q;

public:
    LocationSettingsPrivate(LocationSettings::Mode mode, LocationSettings *settings);
    ~LocationSettingsPrivate();

    LocationSettings::LocationMode calculateLocationMode() const;
    void writeSettings();

    bool mlsAvailable() const;
    bool yandexLocatorAvailable() const;
    bool hereAvailable() const;

    QFileSystemWatcher m_watcher;
    bool m_locationEnabled;
    bool m_gpsEnabled;
    bool m_mlsEnabled;
    bool m_yandexLocatorEnabled;
    LocationSettings::OnlineAGpsState m_mlsOnlineState;
    LocationSettings::OnlineAGpsState m_yandexLocatorOnlineState;
    LocationSettings::OnlineAGpsState m_hereState;
    LocationSettings::LocationMode m_locationMode;
    bool m_settingLocationMode;
    bool m_settingMultipleSettings;
    LocationSettings::DataSources m_allowedDataSources;
    NetworkManager *m_connMan;
    NetworkTechnology *m_gpsTech;
    QDBusInterface *m_gpsTechInterface;

private slots:
    void readSettings();
    void findGpsTech();
    void gpsTechPropertyChanged(const QString &propertyName, const QVariant &value);
    void recalculateLocationMode();
};

// TODO: replace this with DBus calls to a central settings service...
class IniFile
{
public:
    IniFile(const QString &fileName);
    ~IniFile();

    bool isValid() const;
    bool readBool(const QString &section, const QString &key, bool *value, bool defaultValue = false);
    void writeBool(const QString &section, const QString &key, bool value);
    void writeString(const QString &section, const QString &key, const QString &value);

private:
    mutable QScopedPointer<Sailfish::KeyProvider::ProcessMutex> m_processMutex;
    QString m_fileName;
    GKeyFile *m_keyFile;
    GError *m_error;
    bool m_modified;
    bool m_valid;
};

#endif // NEMO_SYSTEMSETTINGS_LOCATIONSETTINGS_P_H
