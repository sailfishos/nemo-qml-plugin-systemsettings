/*
 * Copyright (c) 2017 - 2022 Jolla Ltd.
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

#ifndef NEMO_SYSTEMSETTINGS_DEVICEINFO_H_
#define NEMO_SYSTEMSETTINGS_DEVICEINFO_H_

#include <QObject>

#include "systemsettingsglobal.h"

class QQmlEngine;
class QJSEngine;
class DeviceInfoPrivate;

class SYSTEMSETTINGS_EXPORT DeviceInfo: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString model READ model CONSTANT)
    Q_PROPERTY(QString baseModel READ baseModel CONSTANT)
    Q_PROPERTY(QString designation READ designation CONSTANT)
    Q_PROPERTY(QString manufacturer READ manufacturer CONSTANT)
    Q_PROPERTY(QString prettyName READ prettyName CONSTANT)
    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)
    Q_PROPERTY(QString adaptationVersion READ adaptationVersion CONSTANT)
    Q_PROPERTY(QStringList imeiNumbers READ imeiNumbers NOTIFY imeiNumbersChanged)
    Q_PROPERTY(QString wlanMacAddress READ wlanMacAddress CONSTANT)

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
        FeatureBluetoothTethering,
    };
    Q_ENUM(Feature)

    DeviceInfo(bool synchronousInit, QObject *parent = nullptr);
    DeviceInfo(QObject *parent = nullptr);
    ~DeviceInfo();
    Q_INVOKABLE bool hasFeature(DeviceInfo::Feature feature) const;
    Q_INVOKABLE bool hasHardwareKey(Qt::Key key) const;

    /*!
     * Device model
     *
     * Returns values such as:
     *   "SbJ"
     *   "tbj"
     *   "l500d"
     *   "tk7001"
     *   "SDK"
     *   "SDK Target"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   SsuDeviceInfo::deviceModel()
     */
    QString model() const;

    /*!
     * Device base model
     *
     * If the device is not an variant and there is no base model,
     * returns "UNKNOWN" - otherwise return values are similar as
     * what can be expected from #model().
     *
     * Should be functionally equivalent with:
     *   SsuDeviceInfo::deviceVariant(false)
     */
    QString baseModel() const;

    /*!
     * Type designation, like NCC-1701.
     *
     * Returns values such as:
     *   "JP-1301"
     *   "JT-1501"
     *   "Aqua Fish"
     *   "TK7001"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   SsuDeviceInfo::displayName(Ssu::DeviceDesignation)
     *   QDeviceInfo::productName()
     */
    QString designation() const;

    /*!
     * Manufacturer, like ACME Corp.
     *
     * Returns values such as:
     *   "Jolla"
     *   "Intex"
     *   "Turing Robotic Industries"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   SsuDeviceInfo::displayName(Ssu::DeviceManufacturer)
     *   QDeviceInfo::manufacturer()
     */
    QString manufacturer() const;

    /*!
     * Marketed device name, like Pogoblaster 3000.
     *
     * Returns values such as:
     *   "Jolla"
     *   "Jolla Tablet"
     *   "Intex Aqua Fish"
     *   "Turing Phone"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   SsuDeviceInfo::displayName(Ssu::Ssu::DeviceModel)
     *   QDeviceInfo::model()
     */
    QString prettyName() const;

    /*!
     * Operating system name
     *
     * Returns values such as:
     *   "Sailfish OS"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   QDeviceInfo::operatingSystemName()
     */
    QString osName() const;

    /*!
     * Operating system version
     *
     * Returns values such as:
     *   "4.2.0.10"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   QDeviceInfo::version(Os)
     */
    QString osVersion() const;

    /*!
     * Hardware adaptation version
     *
     * Returns values such as:
     *   "4.2.0.10"
     *   "UNKNOWN"
     *
     * Should be functionally equivalent with:
     *   QDeviceInfo::version(Firmware)
     */
    QString adaptationVersion() const;

    /*!
     * List of IMEI number strings
     *
     * Note: Access is limited to users in
     * \l {https://github.com/sailfishos/sailfish-setup} {sailfish-phone}
     * group. Additionally sandboxed applications need
     * \l {https://github.com/sailfishos/sailjail-permissions} {Phone}
     * permission. If these requirements are not met, an empty list
     * is returned.
     *
     * Obtained by enumerating modems exposed on D-Bus by OFono service.
     *
     * Normally enumeration is done asynchronously in background and
     * empty list is returned until enumeration is finished.
     *
     * If DeviceInfo object is created using synchronousInit=true,
     * constructor blocks while getting the initial values - which may
     * be useful when dealing with procedural logic.
     *
     * If/when OFono is not running, an empty list is returned regardless
     * of whether asynchronous or synchronous init is used.
     *
     * Should be functionally equivalent with obtaining data via:
     *   QDeviceInfo::imei()
     *   QDeviceInfo::imeiCount()
     */
    QStringList imeiNumbers();

    /*!
     * Get wlan mac address
     *
     * Note: Interface availability is cached on the first call, but
     *       mac address itself is re-read from sysfs on every call.
     */
    QString wlanMacAddress();

Q_SIGNALS:
    void imeiNumbersChanged();

private:

    DeviceInfoPrivate *d_ptr;
    Q_DISABLE_COPY(DeviceInfo)
    Q_DECLARE_PRIVATE(DeviceInfo)
};

#endif /* NEMO_SYSTEMSETTINGS_DEVICEINFO_H_ */
