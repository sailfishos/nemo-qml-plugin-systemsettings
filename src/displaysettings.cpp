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

#include <mce/dbus-names.h>
#include <mce/mode-names.h>
#include "mceiface.h"
#include "displaysettings.h"
#include <QDebug>

DisplaySettings::DisplaySettings(QObject *parent)
    : QObject(parent)
{
    m_mceIface = new ComNokiaMceRequestInterface(MCE_SERVICE, MCE_REQUEST_PATH, QDBusConnection::systemBus(), this);
    QDBusPendingReply<QDBusVariant> result = m_mceIface->get_config(QDBusObjectPath("/system/osso/dsm/display/display_brightness"));
    result.waitForFinished();

    m_brightness = result.value().variant().toInt();
}

int DisplaySettings::brightness()
{
    return m_brightness;
}

void DisplaySettings::setBrightness(int value)
{
    if (m_brightness != value) {
        m_brightness = value;
        m_mceIface->set_config(QDBusObjectPath("/system/osso/dsm/display/display_brightness"), QDBusVariant(value));
        emit brightnessChanged();
    }
}

int DisplaySettings::maximumBrightness()
{
    QDBusPendingReply<QDBusVariant> result = m_mceIface->get_config(QDBusObjectPath("/system/osso/dsm/display/max_display_brightness_levels"));
    result.waitForFinished();

    return result.value().variant().toInt();
}

