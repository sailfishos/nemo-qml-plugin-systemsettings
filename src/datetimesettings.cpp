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
#include <QDebug>

DateTimeSettings::DateTimeSettings(QObject *parent)
    : QObject(parent),
      m_autoSystemTime(m_time.autoSystemTime()),
      m_autoTimezone(m_time.autoTimeZone())
{
    m_time.getTimezone(m_timezone);
    connect(&m_time, SIGNAL(timeOrSettingsChanged(MeeGo::QmTime::WhatChanged)),
            this, SLOT(handleTimeChanged(MeeGo::QmTime::WhatChanged)));
}

DateTimeSettings::~DateTimeSettings()
{
}

void DateTimeSettings::setTime(int hour, int minute)
{
    QDate currentDate = QDate::currentDate();
    QTime time(hour, minute);
    QDateTime newTime(currentDate, time);
    m_time.setTime(newTime.toTime_t());
}


void DateTimeSettings::setDate(const QDate &date)
{
    QDateTime newTime = QDateTime::currentDateTime();
    newTime.setDate(date);
    m_time.setTime(newTime.toTime_t());
}

bool DateTimeSettings::automaticTimeUpdate()
{
    return m_autoSystemTime == MeeGo::QmTime::AutoSystemTimeOn;
}

void DateTimeSettings::setAutomaticTimeUpdate(bool enable)
{
    m_time.setAutoSystemTime(enable ? MeeGo::QmTime::AutoSystemTimeOn : MeeGo::QmTime::AutoSystemTimeOff);
}

bool DateTimeSettings::automaticTimezoneUpdate()
{
    return m_autoTimezone == MeeGo::QmTime::AutoTimeZoneOn;
}

void DateTimeSettings::setAutomaticTimezoneUpdate(bool enable)
{
    bool enabled = m_autoTimezone == MeeGo::QmTime::AutoTimeZoneOn;
    if (enabled == enable) {
        return;
    }

    m_time.setAutoTimeZone(enable ? MeeGo::QmTime::AutoTimeZoneOn : MeeGo::QmTime::AutoTimeZoneOff);
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

    m_time.setTimezone(tz);
}

void DateTimeSettings::handleTimeChanged(MeeGo::QmTime::WhatChanged what)
{
    switch (what) {
    case MeeGo::QmTime::TimeChanged:
        emit timeChanged();
        // fall through
    case MeeGo::QmTime::OnlySettingsChanged:
    {
        MeeGo::QmTime::AutoSystemTimeStatus newAutoSystemTime = m_time.autoSystemTime();
        if (newAutoSystemTime != m_autoSystemTime) {
            m_autoSystemTime = newAutoSystemTime;
            emit automaticTimeUpdateChanged();
        }
        MeeGo::QmTime::AutoTimeZoneStatus newAutoTimezone = m_time.autoTimeZone();
        if (newAutoTimezone != m_autoTimezone) {
            m_autoTimezone = newAutoTimezone;
            emit automaticTimezoneUpdateChanged();
        }
        QString newTimezone;
        if (m_time.getTimezone(newTimezone) && newTimezone != m_timezone) {
            m_timezone = newTimezone;
            emit timezoneChanged();
        }
    }
    }
}
