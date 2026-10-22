/*
 * Copyright (C) 2026 Jolla Mobile Ltd.
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

#include "keypadsettings.h"

#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include "mceiface.h"
#include <QDebug>

static const char MceKeypadBacklight[] = "/system/osso/dsm/keypadbacklight/enabled";

KeypadSettings::KeypadSettings(QObject *parent)
    : QObject(parent)
{
    m_mceSignalIface = new ComNokiaMceSignalInterface(MCE_SERVICE, MCE_SIGNAL_PATH,
                                                      QDBusConnection::systemBus(), this);
    connect(m_mceSignalIface, &ComNokiaMceSignalInterface::config_change_ind,
            this, &KeypadSettings::configChange);

    m_mceIface = new ComNokiaMceRequestInterface(MCE_SERVICE, MCE_REQUEST_PATH,
                                                 QDBusConnection::systemBus(), this);
    QDBusPendingReply<QVariantMap> call = m_mceIface->get_config_all();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     this, &KeypadSettings::configReply);
}

bool KeypadSettings::populated() const
{
    return m_populated;
}

bool KeypadSettings::backlightEnabled() const
{
    return m_backlightEnabled;
}

void KeypadSettings::setBacklightEnabled(bool value)
{
    if (m_backlightEnabled != value) {
        m_backlightEnabled = value;
        m_mceIface->set_config(QDBusObjectPath(MceKeypadBacklight), QDBusVariant(value));
        emit backlightEnabledChanged();
    }
}

void KeypadSettings::updateConfig(const QString &key, const QVariant &value)
{
    if (key == MceKeypadBacklight) {
        bool val = value.toBool();
        if (m_backlightEnabled != val) {
            m_backlightEnabled = val;
            emit backlightEnabledChanged();
        }
    }
}

void KeypadSettings::configReply(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not retrieve mce settings: '%s'",
                 reply.error().message().toStdString().c_str());
    } else {
        QVariantMap map = reply.value();
        QMapIterator<QString, QVariant> i(map);
        while (i.hasNext()) {
            i.next();
            updateConfig(i.key(), i.value());
        }
        m_populated = true;
        emit populatedChanged();
    }

    watcher->deleteLater();
}

void KeypadSettings::configChange(const QString &key, const QDBusVariant &value)
{
    updateConfig(key, value.variant());
}
