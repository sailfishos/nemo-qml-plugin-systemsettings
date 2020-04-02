/*
 * Copyright (C) 2013-2015 Jolla Ltd. <pekka.vuorela@jollamobile.com>
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

#include <QtGlobal>
#include <QtQml>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QTranslator>

#include <qusbmoded.h>

#include "languagemodel.h"
#include "datetimesettings.h"
#include "profilecontrol.h"
#include "alarmtonemodel.h"
#include "displaysettings.h"
#include "aboutsettings.h"
#include "developermodesettings.h"
#include "batterystatus.h"
#include "diskusage.h"
#include "partitionmodel.h"
#include "certificatemodel.h"
#include "settingsvpnmodel.h"
#include "locationsettings.h"
#include "deviceinfo.h"
#include "nfcsettings.h"
#include "userinfo.h"
#include "usermodel.h"

class AppTranslator: public QTranslator
{
    Q_OBJECT
public:
    AppTranslator(QObject *parent)
        : QTranslator(parent)
    {
        qApp->installTranslator(this);
    }

    virtual ~AppTranslator()
    {
        qApp->removeTranslator(this);
    }
};

template<class T>
static QObject *api_factory(QQmlEngine *, QJSEngine *)
{
    return new T;
}

class SystemSettingsPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.nemomobile.systemsettings")

public:
    void initializeEngine(QQmlEngine *engine, const char *uri)
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("org.nemomobile.systemsettings"));
        AppTranslator *engineeringEnglish = new AppTranslator(engine);
        engineeringEnglish->load("qml_plugin_systemsettings_eng_en", "/usr/share/translations");
        AppTranslator *translator = new AppTranslator(engine);
        translator->load(QLocale(), "qml_plugin_systemsettings", "-", "/usr/share/translations");
    }

    void registerTypes(const char *uri)
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("org.nemomobile.systemsettings"));
        qmlRegisterType<LanguageModel>(uri, 1, 0, "LanguageModel");
        qmlRegisterType<DateTimeSettings>(uri, 1, 0, "DateTimeSettings");
        qmlRegisterType<ProfileControl>(uri, 1, 0, "ProfileControl");
        qmlRegisterType<AlarmToneModel>(uri, 1, 0, "AlarmToneModel");
        qmlRegisterType<DisplaySettings>(uri, 1, 0, "DisplaySettings");
        qmlRegisterType<QUsbModed>(uri, 1, 0, "USBSettings");
        qmlRegisterType<AboutSettings>(uri, 1, 0, "AboutSettings");
        qmlRegisterType<PartitionModel>(uri, 1, 0, "PartitionModel");
        qRegisterMetaType<Partition>("Partition");
        qmlRegisterType<DeveloperModeSettings>(uri, 1, 0, "DeveloperModeSettings");
        qmlRegisterType<CertificateModel>(uri, 1, 0, "CertificateModel");
        qmlRegisterSingletonType<SettingsVpnModel>(uri, 1, 0, "SettingsVpnModel", api_factory<SettingsVpnModel>);
        qRegisterMetaType<DeveloperModeSettings::Status>("DeveloperModeSettings::Status");
        qmlRegisterType<BatteryStatus>(uri, 1, 0, "BatteryStatus");
        qmlRegisterType<DiskUsage>(uri, 1, 0, "DiskUsage");
        qmlRegisterType<LocationSettings>(uri, 1, 0, "LocationSettings");
        qmlRegisterType<DeviceInfo>(uri, 1, 0, "DeviceInfo");
        qmlRegisterType<NfcSettings>(uri, 1, 0, "NfcSettings");
        qmlRegisterType<UserInfo>(uri, 1, 0, "UserInfo");
        qmlRegisterType<UserModel>(uri, 1, 0, "UserModel");
    }
};

#include "plugin.moc"
