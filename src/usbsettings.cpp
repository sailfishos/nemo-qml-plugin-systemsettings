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

#include "usbsettings.h"

using namespace MeeGo;

// mapping from QmUSBMode default mode to USBSettings default mode
static USBSettings::Mode qm2settings(QmUSBMode::Mode mode)
{
    switch (mode) {
    case QmUSBMode::Ask:
        return USBSettings::AskMode;
    case QmUSBMode::MassStorage:
        return USBSettings::MassStorageMode;
    case QmUSBMode::Developer:
        return USBSettings::DeveloperMode;
    case QmUSBMode::MTP:
        return USBSettings::MTPMode;
    case QmUSBMode::ChargingOnly:
        return USBSettings::ChargingMode;
    default:
        return USBSettings::AskMode;
    }
}

// mapping from USBSettings default mode to QmUSBMode default mode
static QmUSBMode::Mode settings2qm(USBSettings::Mode mode)
{
    switch(mode) {
    case USBSettings::AskMode:
        return QmUSBMode::Ask;
    case USBSettings::MassStorageMode:
        return QmUSBMode::MassStorage;
    case USBSettings::DeveloperMode:
        return QmUSBMode::Developer;
    case USBSettings::MTPMode:
        return QmUSBMode::MTP;
    case USBSettings::ChargingMode:
        return QmUSBMode::ChargingOnly;
    case USBSettings::UndefinedMode:
        return QmUSBMode::Undefined;
    default:
        qWarning("Unknown USB mode");
        return QmUSBMode::Undefined;
    }
}

USBSettings::USBSettings(QObject *parent)
    : QObject(parent),
      m_qmmode(new QmUSBMode(this))
{
    connect(m_qmmode, SIGNAL(modeChanged(MeeGo::QmUSBMode::Mode)),
            this, SIGNAL(currentModeChanged()));
}

USBSettings::~USBSettings()
{
}

USBSettings::Mode USBSettings::currentMode() const
{
    QmUSBMode::Mode active = m_qmmode->getMode();

    switch (active) {
    case QmUSBMode::MassStorage:
        return USBSettings::MassStorageMode;

    case QmUSBMode::Developer:
        return USBSettings::DeveloperMode;

    case QmUSBMode::MTP:
        return USBSettings::MTPMode;

    case QmUSBMode::ModeRequest:
    case QmUSBMode::Ask:
    case QmUSBMode::ChargingOnly:
        return USBSettings::ChargingMode;

    case QmUSBMode::Disconnected:
    case QmUSBMode::Undefined:
        return USBSettings::UndefinedMode;

    default:
        qWarning("Unhandled USB mode: %d", active);
    }

    return USBSettings::UndefinedMode;
}

USBSettings::Mode USBSettings::defaultMode() const
{
    return qm2settings(m_qmmode->getDefaultMode());
}

void USBSettings::setDefaultMode(const USBSettings::Mode mode)
{
    if (mode == defaultMode()) {
        return;
    }

    if (m_qmmode->setDefaultMode(settings2qm(mode))) {
        emit defaultModeChanged();
    } else {
        qWarning("Couldn't set default mode");
    }
}
