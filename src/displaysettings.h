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

#ifndef DISPLAYSETTINGS_H
#define DISPLAYSETTINGS_H

#include <QObject>
#include <QtQml>

class ComNokiaMceRequestInterface;
class ComNokiaMceSignalInterface;
class QDBusVariant;

class DisplaySettings: public QObject
{
    Q_OBJECT

    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int maximumBrightness READ maximumBrightness CONSTANT)
    Q_PROPERTY(int dimTimeout READ dimTimeout WRITE setDimTimeout NOTIFY dimTimeoutChanged)
    Q_PROPERTY(int blankTimeout READ blankTimeout WRITE setBlankTimeout NOTIFY blankTimeoutChanged)
    Q_PROPERTY(bool adaptiveDimmingEnabled READ adaptiveDimmingEnabled WRITE setAdaptiveDimmingEnabled NOTIFY adaptiveDimmingEnabledChanged)
    Q_PROPERTY(bool ambientLightSensorEnabled READ ambientLightSensorEnabled WRITE setAmbientLightSensorEnabled NOTIFY ambientLightSensorEnabledChanged)

public:
    explicit DisplaySettings(QObject *parent = 0);

    int brightness() const;
    void setBrightness(int);

    int maximumBrightness();

    int dimTimeout() const;
    void setDimTimeout(int t);

    int blankTimeout() const;
    void setBlankTimeout(int t);

    bool adaptiveDimmingEnabled() const;
    void setAdaptiveDimmingEnabled(bool);

    bool ambientLightSensorEnabled() const;
    void setAmbientLightSensorEnabled(bool);

signals:
    void brightnessChanged();
    void dimTimeoutChanged();
    void blankTimeoutChanged();
    void adaptiveDimmingEnabledChanged();
    void ambientLightSensorEnabledChanged();

private slots:
    void configChange(const QString &key, const QDBusVariant &value);

private:
    ComNokiaMceRequestInterface *m_mceIface;
    ComNokiaMceSignalInterface *m_mceSignalIface;
    int m_brightness;
    int m_dimTimeout;
    int m_blankTimeout;
    bool m_adaptiveDimmingEnabled;
    bool m_ambientLightSensorEnabled;
};

QML_DECLARE_TYPE(DisplaySettings)

#endif
