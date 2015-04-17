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

#include "usb_moded-dbus.h"
#include "usb_moded-modes.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>


static QDBusMessage call_usb_moded(const QString &method, const QList<QVariant> &arguments=QList<QVariant>())
{
    QDBusMessage methodCall = QDBusMessage::createMethodCall(USB_MODE_SERVICE, USB_MODE_OBJECT,
            USB_MODE_INTERFACE, method);
    methodCall.setArguments(arguments);
    return QDBusConnection::systemBus().call(methodCall);
}

static const struct {
    const char *name;
    enum USBSettings::Mode mode;
} mode_mapping[] = {
    // States (from usb_moded-dbus.h)
    { USB_CONNECTED, USBSettings::Connected },
    { DATA_IN_USE, USBSettings::DataInUse },
    { USB_DISCONNECTED, USBSettings::Disconnected },
    { USB_CONNECTED_DIALOG_SHOW, USBSettings::ModeRequest },

    // Modes (from usb_moded-modes.h)
    { MODE_MASS_STORAGE, USBSettings::MassStorage },
    { MODE_CHARGING, USBSettings::ChargingOnly },
    { MODE_CHARGING_FALLBACK, USBSettings::ChargingOnly },
    { MODE_PC_SUITE, USBSettings::PCSuite },
    { MODE_ASK, USBSettings::Ask },
    { MODE_UNDEFINED, USBSettings::Undefined },
    { MODE_DEVELOPER, USBSettings::Developer },
    { MODE_MTP, USBSettings::MTP },
    { MODE_ADB, USBSettings::Adb },
    { MODE_DIAG, USBSettings::Diag },
    { MODE_CONNECTION_SHARING, USBSettings::ConnectionSharing },
    { MODE_HOST, USBSettings::Host },
    { MODE_CHARGER, USBSettings::Charger },
};

#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof((x)[0]))

static enum USBSettings::Mode decodeMode(const QString &name)
{
    for (int i=0; i<ARRAY_SIZE(mode_mapping); i++) {
        if (name == mode_mapping[i].name) {
            return mode_mapping[i].mode;
        }
    }

    return USBSettings::Undefined;
}

static QString encodeMode(enum USBSettings::Mode mode)
{
    for (int i=0; i<ARRAY_SIZE(mode_mapping); i++) {
        if (mode == mode_mapping[i].mode) {
            return QString(mode_mapping[i].name);
        }
    }

    return MODE_UNDEFINED;
}


USBSettings::USBSettings(QObject *parent)
    : QObject(parent)
    , m_supportedUSBModes()
{
    // TODO: We could connect to USB_MODE_SUPPORTED_MODES_SIGNAL_NAME and update the internal
    // list plus emit supportedUSBModesChanged()
    QDBusReply<QString> supportedModes = call_usb_moded("get_modes");
    foreach (QString part, supportedModes.value().split(',')) {
        m_supportedUSBModes << decodeMode(part.trimmed());
    }

    QDBusConnection::systemBus().connect(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE,
            USB_MODE_SIGNAL_NAME, this, SLOT(currentModeChanged()));

    // TODO: We could watch the default mode (no signal yet in usb_moded) and
    // emit defaultModeChanged() so that the settings UI is always up to date
}

USBSettings::~USBSettings()
{
}

USBSettings::Mode USBSettings::currentMode() const
{
    QDBusReply<QString> reply = call_usb_moded("mode_request");
    return reply.isValid() ? decodeMode(reply.value()) : USBSettings::Undefined;
}

USBSettings::Mode USBSettings::defaultMode() const
{
    QDBusReply<QString> reply = call_usb_moded("get_config");
    return reply.isValid() ? decodeMode(reply.value()) : USBSettings::Undefined;
}

QList<int> USBSettings::supportedUSBModes() const
{
    return m_supportedUSBModes;
}

void USBSettings::setDefaultMode(const Mode mode)
{
    if (mode == defaultMode()) {
        return;
    }

    QDBusReply<QString> reply = call_usb_moded("set_config", QVariantList() << encodeMode(mode));
    if (reply.isValid() && decodeMode(reply.value()) == mode) {
        emit defaultModeChanged();
    } else {
        qWarning("Couldn't set default mode");
    }
}

void USBSettings::setCurrentMode(const Mode mode)
{
    QDBusMessage call = QDBusMessage::createMethodCall(USB_MODE_SERVICE, USB_MODE_OBJECT, USB_MODE_INTERFACE, USB_MODE_STATE_SET);
    call << encodeMode(mode);
    QDBusConnection::systemBus().call(call, QDBus::NoBlock);
}
