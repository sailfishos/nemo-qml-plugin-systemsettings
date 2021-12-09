/*
 * Copyright (C) 2019 Open Mobile Platform LLС. <s.chupligin@omprussia.ru>
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


#include "nfcsettings.h"

#include <nemo-dbus/response.h>

#include <QDBusConnectionInterface>
#include <QDBusConnection>
#include <QDebug>

NfcSettings::NfcSettings(QObject *parent)
    : QObject(parent)
    , m_valid(false)
    , m_enabled(false)
    , m_available(false)
    , m_interface(
            this, QDBusConnection::systemBus(),
            "org.sailfishos.nfc.settings",
            "/",
            "org.sailfishos.nfc.Settings")
{
    if (QDBusConnection::systemBus().interface()->isServiceRegistered("org.sailfishos.nfc.settings")) {
        m_available = true;
        emit availableChanged();

        NemoDBus::Response *response = m_interface.call(QLatin1String("GetEnabled"));
        response->onError([this](const QDBusError &error) {
            qWarning() << "Get dbus error:" << error;
        });
        response->onFinished<bool>([this](bool value) {
            updateEnabledState(value);
            m_valid = true;
            emit validChanged();
        });

        QDBusConnection::systemBus().connect("org.sailfishos.nfc.settings", "/", "org.sailfishos.nfc.Settings", "EnabledChanged", this, SLOT(updateEnabledState(bool)));

    } else {
        qWarning() << "NFC interface not available";
        qWarning() << QDBusConnection::systemBus().interface()->lastError();
    }
}

NfcSettings::~NfcSettings()
{
}

bool NfcSettings::valid() const
{
    return m_valid;
}

bool NfcSettings::available() const
{
    return m_available;
}

bool NfcSettings::enabled() const
{
    return m_enabled;
}

void NfcSettings::setEnabled(bool enabled)
{
    NemoDBus::Response *response = m_interface.call("SetEnabled", enabled);
    response->onError([this](const QDBusError &error) {
        qWarning() << "Set dbus error:" << error;
    });
}

void NfcSettings::updateEnabledState(bool enabled)
{
    if (enabled != m_enabled) {
        m_enabled = enabled;
        emit enabledChanged();
    }
}
