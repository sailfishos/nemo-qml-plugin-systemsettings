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

#include "deviceinfo.h"

#include <QSet>

#include <ssusysinfo.h>
#include <qofonomanager.h>
#include <qofonomodem.h>

class DeviceInfoPrivate: public QObject
{
    Q_OBJECT
public:
    DeviceInfoPrivate(DeviceInfo *deviceInfo, bool synchronousInit);
    ~DeviceInfoPrivate();
    QStringList imeiNumbers();

    QSet<DeviceInfo::Feature> m_features;
    QSet<Qt::Key> m_keys;
    QString m_model;
    QString m_baseModel;
    QString m_designation;
    QString m_manufacturer;
    QString m_prettyName;
    QString m_osName;
    QString m_osVersion;
    QString m_adaptationVersion;
private slots:
    void modemsChanged(const QStringList &modems);
    void modemSerialChanged(const QString &serial);
    void updateModemProperties();
private:
    void modemAdded(const QString &modem);
    void modemRemoved(const QString &modem);
    QSharedPointer<QOfonoManager> ofonoManager();
    void updateModemPropertiesLater();

    DeviceInfo *q_ptr;
    bool m_synchronousInit;
    QSharedPointer<QOfonoManager> m_ofonoManager;
    QHash<QString, QSharedPointer<QOfonoModem> > m_modemHash;
    QStringList m_modemList;
    QStringList m_imeiNumbers;
    QTimer *m_updateModemPropertiesTimer;
    Q_DISABLE_COPY(DeviceInfoPrivate);
    Q_DECLARE_PUBLIC(DeviceInfo);
};

DeviceInfoPrivate::DeviceInfoPrivate(DeviceInfo *deviceInfo, bool synchronousInit)
    : q_ptr(deviceInfo)
    , m_synchronousInit(synchronousInit)
    , m_updateModemPropertiesTimer(nullptr)
{
    ssusysinfo_t *si = ssusysinfo_create();

    hw_feature_t *features = ssusysinfo_get_hw_features(si);
    if (features) {
        for (size_t i = 0; features[i]; ++i) {
            m_features.insert(static_cast<DeviceInfo::Feature>(features[i]));
        }
        free(features);
    }

    hw_key_t *keys = ssusysinfo_get_hw_keys(si);
    if (keys) {
        for (size_t i = 0; keys[i]; ++i) {
            m_keys.insert(static_cast<Qt::Key>(keys[i]));
        }
        free(keys);
    }

    /* Note: These queries always return non-null C string */
    m_model = ssusysinfo_device_model(si);
    m_baseModel = ssusysinfo_device_base_model(si);
    m_designation = ssusysinfo_device_designation(si);
    m_manufacturer = ssusysinfo_device_manufacturer(si);
    m_prettyName = ssusysinfo_device_pretty_name(si);
    m_osName = ssusysinfo_os_name(si);
    m_osVersion = ssusysinfo_os_version(si);
    m_adaptationVersion = ssusysinfo_hw_version(si);

    ssusysinfo_delete(si);
}

DeviceInfoPrivate::~DeviceInfoPrivate()
{
}

QStringList DeviceInfoPrivate::imeiNumbers()
{
    /* Trigger on-demand ofono tracking and
     * evaluate initial property values. */
    ofonoManager();

    return m_imeiNumbers;
}

QSharedPointer<QOfonoManager> DeviceInfoPrivate::ofonoManager()
{
    if (m_ofonoManager.isNull()) {
        m_ofonoManager = QOfonoManager::instance(m_synchronousInit);
        connect(&*m_ofonoManager, &QOfonoManager::modemsChanged, this, &DeviceInfoPrivate::modemsChanged);

        m_updateModemPropertiesTimer = new QTimer(this);
        m_updateModemPropertiesTimer->setInterval(50);
        m_updateModemPropertiesTimer->setSingleShot(true);
        connect(m_updateModemPropertiesTimer, &QTimer::timeout, this, &DeviceInfoPrivate::updateModemProperties);

        QStringList modems(m_ofonoManager->modems());
        for (auto iter = modems.cbegin(); iter != modems.cend(); ++iter)
            modemAdded(*iter);
        updateModemProperties();
    }
    return m_ofonoManager;
}

void DeviceInfoPrivate::updateModemPropertiesLater()
{
    if (!m_updateModemPropertiesTimer->isActive())
        m_updateModemPropertiesTimer->start();
}

void DeviceInfoPrivate::updateModemProperties()
{
    QStringList imeiNumbers;
    for (auto iter = m_modemList.cbegin(); iter != m_modemList.cend(); ++iter) {
        QString imei(m_modemHash[*iter]->serial());
        if (!imei.isEmpty())
            imeiNumbers.append(imei);
    }
    if (m_imeiNumbers != imeiNumbers) {
        m_imeiNumbers = imeiNumbers;
        Q_Q(DeviceInfo);
        emit q->imeiNumbersChanged();
    }
    m_updateModemPropertiesTimer->stop();
}

void DeviceInfoPrivate::modemRemoved(const QString &modemName)
{
    QSharedPointer<QOfonoModem> modem(m_modemHash.take(modemName));
    if (!modem.isNull()) {
        disconnect(&*modem, &QOfonoModem::serialChanged, this, &DeviceInfoPrivate::modemSerialChanged);
        m_modemList.removeOne(modemName);
        updateModemPropertiesLater();
    }
}

void DeviceInfoPrivate::modemAdded(const QString &modemName)
{
    if (!m_modemHash.contains(modemName)) {
        QSharedPointer<QOfonoModem> modem(QOfonoModem::instance(modemName, m_synchronousInit));
        connect(&*modem, &QOfonoModem::serialChanged, this, &DeviceInfoPrivate::modemSerialChanged);
        m_modemHash[modemName] = modem;
        m_modemList.append(modemName);
        updateModemPropertiesLater();
    }
}

void DeviceInfoPrivate::modemsChanged(const QStringList &modems)
{
    QSet<QString> previous(m_modemList.toSet());
    QSet<QString> current(modems.toSet());
    QSet<QString> added(current - previous);
    QSet<QString> removed(previous - current);
    for (auto iter = removed.cbegin(); iter != removed.cend(); ++iter)
        modemRemoved(*iter);
    for (auto iter = added.cbegin(); iter != added.cend(); ++iter)
        modemAdded(*iter);
}

void DeviceInfoPrivate::modemSerialChanged(const QString &serial)
{
    Q_UNUSED(serial);
    updateModemPropertiesLater();
}

DeviceInfo::DeviceInfo(bool synchronousInit, QObject *parent)
    : QObject(parent)
    , d_ptr(new DeviceInfoPrivate(this, synchronousInit))
{
    Q_D(const DeviceInfo);
}

DeviceInfo::DeviceInfo(QObject *parent)
    : QObject(parent)
    , d_ptr(new DeviceInfoPrivate(this, false))
{
    Q_D(const DeviceInfo);
}

DeviceInfo::~DeviceInfo()
{
    delete d_ptr;
    d_ptr = 0;
}

bool DeviceInfo::hasFeature(DeviceInfo::Feature feature) const
{
    Q_D(const DeviceInfo);
    return d->m_features.contains(feature);
}

bool DeviceInfo::hasHardwareKey(Qt::Key key) const
{
    Q_D(const DeviceInfo);
    return d->m_keys.contains(key);
}

QString DeviceInfo::model() const
{
    Q_D(const DeviceInfo);
    return d->m_model;
}

QString DeviceInfo::baseModel() const
{
    Q_D(const DeviceInfo);
    return d->m_baseModel;
}

QString DeviceInfo::designation() const
{
    Q_D(const DeviceInfo);
    return d->m_designation;
}

QString DeviceInfo::manufacturer() const
{
    Q_D(const DeviceInfo);
    return d->m_manufacturer;
}

QString DeviceInfo::prettyName() const
{
    Q_D(const DeviceInfo);
    return d->m_prettyName;
}

QString DeviceInfo::osName() const
{
    Q_D(const DeviceInfo);
    return d->m_osName;
}

QString DeviceInfo::osVersion() const
{
    Q_D(const DeviceInfo);
    return d->m_osVersion;
}

QString DeviceInfo::adaptationVersion() const
{
    Q_D(const DeviceInfo);
    return d->m_adaptationVersion;
}

QStringList DeviceInfo::imeiNumbers()
{
    Q_D(DeviceInfo);
    return d->imeiNumbers();
}

#include "deviceinfo.moc"
