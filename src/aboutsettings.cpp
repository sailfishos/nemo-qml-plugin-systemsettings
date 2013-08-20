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

#include "aboutsettings.h"

#include <QDebug>
#include <QStringList>
#include <QStorageInfo>
#include <QNetworkInfo>
#include <QDeviceInfo>

AboutSettings::AboutSettings(QObject *parent)
    : QObject(parent),
      m_sysinfo(new QStorageInfo(this)),
      m_netinfo(new QNetworkInfo(this)),
      m_devinfo(new QDeviceInfo(this))
{
    qDebug() << "Drives:" << m_sysinfo->allLogicalDrives();
}

AboutSettings::~AboutSettings()
{
}

qlonglong AboutSettings::totalDiskSpace() const
{
    return m_sysinfo->totalDiskSpace("/");
}

qlonglong AboutSettings::availableDiskSpace() const
{
    return m_sysinfo->availableDiskSpace("/");
}

const QString AboutSettings::bluetoothAddress() const
{
    return m_netinfo->macAddress(QNetworkInfo::BluetoothMode, 0);
}

const QString AboutSettings::wlanMacAddress() const
{
    return m_netinfo->macAddress(QNetworkInfo::WlanMode, 0);
}

const QString AboutSettings::imei() const
{
    return m_devinfo->imei(0);
}

const QString AboutSettings::manufacturer() const
{
    return m_devinfo->manufacturer();
}

const QString AboutSettings::productName() const
{
    return m_devinfo->productName();
}

const QString AboutSettings::model() const
{
    return m_devinfo->model();
}
