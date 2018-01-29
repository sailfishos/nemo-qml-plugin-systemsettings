/*
 * Copyright (C) 2017 Jolla Ltd. <simo.piiroinen@jollamobile.com>
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

#ifndef DEVICEINFO_H
#define DEVICEINFO_H

#include <QObject>

#include "systemsettingsglobal.h"

class QQmlEngine;
class QJSEngine;
class DeviceInfoPrivate;

class SYSTEMSETTINGS_EXPORT DeviceInfo: public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasAndroidSupport READ hasAndroidSupport CONSTANT)

public:
    enum Feature {
        /* Duplicates information declared in ssusysinfo.h to
         * avoid build time dependencies to libssusysinfo and
         * ease use from Qt/QML applications. */
        FeatureInvalid,
        FeatureMicrophone1,
        FeatureMicrophone2,
        FeatureBackCamera,
        FeatureBackCameraFlashlight,
        FeatureDisplayBacklight,
        FeatureBattery,
        FeatureBluetooth,
        FeatureCellularData,
        FeatureCellularVoice,
        FeatureCompassSensor,
        FeatureFMRadioReceiver,
        FeatureFrontCamera,
        FeatureFrontCameraFlashlight,
        FeatureGPS,
        FeatureCellInfo,
        FeatureAccelerationSensor,
        FeatureGyroSensor,
        FeatureCoverSensor,
        FeatureFingerprintSensor,
        FeatureHeadset,
        FeatureHardwareKeys,
        FeatureDisplay,
        FeatureNotificationLED,
        FeatureButtonBacklight,
        FeatureLightSensor,
        FeatureLoudspeaker,
        FeatureTheOtherHalf,
        FeatureProximitySensor,
        FeatureAudioPlayback,
        FeatureMemoryCardSlot,
        FeatureSIMCardSlot,
        FeatureStereoLoudspeaker,
        FeatureTouchScreen,
        FeatureTouchScreenSelfTest,
        FeatureUSBCharging,
        FeatureUSBOTG,
        FeatureVibrator,
        FeatureWLAN,
        FeatureNFC,
        FeatureVideoPlayback,
        FeatureSuspend,
        FeatureReboot,
    };
    Q_ENUM(Feature)

    DeviceInfo(QObject *parent = 0);
    ~DeviceInfo();

    Q_INVOKABLE bool hasFeature(DeviceInfo::Feature feature) const;
    Q_INVOKABLE bool hasHardwareKey(Qt::Key key) const;

    bool hasAndroidSupport() const;

private:

    DeviceInfoPrivate *d_ptr;
    Q_DISABLE_COPY(DeviceInfo)
    Q_DECLARE_PRIVATE(DeviceInfo)
};

#endif /* DEVICEINFO_H */
