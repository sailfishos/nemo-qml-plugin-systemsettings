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

#include "datetimesettings.h"

#include <timed-qt5/interface>
#include <timed-qt5/wallclock>
#include <QDebug>


DateTimeSettings::DateTimeSettings(QObject *parent)
    : QObject(parent)
    , m_timed()
    , m_timezone()
    , m_autoSystemTime(false)
    , m_autoTimezone(false)
    , m_timedInfoValid(false)
    , m_timedInfo()
{
    if (!m_timed.settings_changed_connect(this, SLOT(onTimedSignal(const Maemo::Timed::WallClock::Info &, bool)))) {
        qWarning("Connection to timed signal failed: '%s'", Maemo::Timed::bus().lastError().message().toStdString().c_str());
    }

    // Request the first update of the wall clock info
    updateTimedInfo();
}

DateTimeSettings::~DateTimeSettings()
{
}

void DateTimeSettings::updateTimedInfo()
{
    QDBusPendingCall call = m_timed.get_wall_clock_info_async();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

    QObject::connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher *)),
                     this, SLOT(onGetWallClockInfoFinished(QDBusPendingCallWatcher *)));
}

void DateTimeSettings::onGetWallClockInfoFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<Maemo::Timed::WallClock::Info> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not retrieve wall clock info: '%s'", reply.error().message().toStdString().c_str());
    } else {
        onTimedSignal(reply.value(), false);
    }

    watcher->deleteLater();
}

void DateTimeSettings::setTime(int hour, int minute)
{
    QDate currentDate = QDate::currentDate();
    QTime time(hour, minute);
    QDateTime newTime(currentDate, time);
    setTime(newTime.toTime_t());
}


void DateTimeSettings::setDate(const QDate &date)
{
    QDateTime newTime = QDateTime::currentDateTime();
    newTime.setDate(date);
    setTime(newTime.toTime_t());
}

bool DateTimeSettings::automaticTimeUpdate()
{
    return m_autoSystemTime;
}

void DateTimeSettings::setAutomaticTimeUpdate(bool enable)
{
    if (enable != m_autoSystemTime) {
        Maemo::Timed::WallClock::Settings s;

        if (enable) {
            s.setTimeNitz();
        } else {
            s.setTimeManual();
        }

        setSettings(s);
    }
}

bool DateTimeSettings::automaticTimezoneUpdate()
{
    return m_autoTimezone;
}

void DateTimeSettings::setAutomaticTimezoneUpdate(bool enable)
{
    if (enable != m_autoTimezone) {
        Maemo::Timed::WallClock::Settings s;

        if (enable) {
            s.setTimezoneCellular();
        } else {
            s.setTimezoneManual("");
        }

        setSettings(s);
    }
}

QString DateTimeSettings::timezone() const
{
    return m_timezone;
}

void DateTimeSettings::setTimezone(const QString &tz)
{
    if (tz == m_timezone) {
        return;
    }

    Maemo::Timed::WallClock::Settings s;
    s.setTimezoneManual(tz);
    setSettings(s);
}

void DateTimeSettings::setHourMode(DateTimeSettings::HourMode mode)
{
    Maemo::Timed::WallClock::Settings s;
    s.setFlag24(mode == TwentyFourHours);
    setSettings(s);
}

void DateTimeSettings::onWallClockSettingsFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<bool> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not set wall clock settings: '%s'", reply.error().message().toStdString().c_str());
    } else if (!reply.value()) {
        qWarning("Could not set wall clock settings");
    }

    watcher->deleteLater();
}

bool DateTimeSettings::setSettings(Maemo::Timed::WallClock::Settings &s)
{
    if (!s.check()) {
        return false;
    }

    QDBusPendingCall call = m_timed.wall_clock_settings_async(s);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

    QObject::connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher *)),
                     this, SLOT(onWallClockSettingsFinished(QDBusPendingCallWatcher *)));

    return true;
}

bool DateTimeSettings::setTime(time_t time)
{
    Maemo::Timed::WallClock::Settings s;
    s.setTimeManual(time);
    return setSettings(s);
}

void DateTimeSettings::onTimedSignal(const Maemo::Timed::WallClock::Info &info, bool time_changed)
{
    m_timedInfo = info;
    m_timedInfoValid = true;

    if (time_changed) {
        emit timeChanged();
    }

    bool newAutoSystemTime = info.flagTimeNitz();
    if (newAutoSystemTime != m_autoSystemTime) {
        m_autoSystemTime = newAutoSystemTime;
        emit automaticTimeUpdateChanged();
    }

    bool newAutoTimezone = info.flagLocalCellular();
    if (newAutoTimezone != m_autoTimezone) {
        m_autoTimezone = newAutoTimezone;
        emit automaticTimezoneUpdateChanged();
    }

    QString newTimezone = info.humanReadableTz();
    if (newTimezone != m_timezone) {
        m_timezone = newTimezone;
        emit timezoneChanged();
    }
}
