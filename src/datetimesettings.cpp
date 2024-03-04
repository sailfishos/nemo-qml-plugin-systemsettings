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

class DateTimeSettingsPrivate: public QObject
{
    Q_OBJECT
public:
    DateTimeSettingsPrivate(DateTimeSettings *parent);
    virtual ~DateTimeSettingsPrivate() {}

public slots:
    void onTimedSignal(const Maemo::Timed::WallClock::Info &info, bool time_changed);

public:
    void onGetWallClockInfoFinished(QDBusPendingCallWatcher *watcher);
    void onWallClockSettingsFinished(QDBusPendingCallWatcher *watcher);

    bool setTime(time_t time);
    bool setSettings(Maemo::Timed::WallClock::Settings &s);
    void updateTimedInfo();

    DateTimeSettings *q;
    Maemo::Timed::Interface m_timed;
    QString m_timezone;
    bool m_autoSystemTime;
    bool m_autoTimezone;
    bool m_timedInfoValid;
    Maemo::Timed::WallClock::Info m_timedInfo;
};

DateTimeSettingsPrivate::DateTimeSettingsPrivate(DateTimeSettings *parent)
    : QObject(parent)
    , q(parent)
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

void DateTimeSettingsPrivate::onTimedSignal(const Maemo::Timed::WallClock::Info &info, bool time_changed)
{
    const bool prevReady = q->ready();

    m_timedInfo = info;
    m_timedInfoValid = true;

    if (time_changed) {
        emit q->timeChanged();
    }

    bool newAutoSystemTime = info.flagTimeNitz();
    if (newAutoSystemTime != m_autoSystemTime) {
        m_autoSystemTime = newAutoSystemTime;
        emit q->automaticTimeUpdateChanged();
    }

    bool newAutoTimezone = info.flagLocalCellular();
    if (newAutoTimezone != m_autoTimezone) {
        m_autoTimezone = newAutoTimezone;
        emit q->automaticTimezoneUpdateChanged();
    }

    QString newTimezone = info.humanReadableTz();
    if (newTimezone != m_timezone) {
        m_timezone = newTimezone;
        emit q->timezoneChanged();
    }

    if (prevReady != q->ready()) {
        emit q->readyChanged();
    }
}

void DateTimeSettingsPrivate::onGetWallClockInfoFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<Maemo::Timed::WallClock::Info> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not retrieve wall clock info: '%s'", reply.error().message().toStdString().c_str());
    } else {
        onTimedSignal(reply.value(), false);
    }

    watcher->deleteLater();
}

void DateTimeSettingsPrivate::onWallClockSettingsFinished(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<bool> reply = *watcher;

    if (reply.isError()) {
        qWarning("Could not set wall clock settings: '%s'", reply.error().message().toStdString().c_str());
    } else if (!reply.value()) {
        qWarning("Could not set wall clock settings");
    }

    watcher->deleteLater();
}

bool DateTimeSettingsPrivate::setTime(time_t time)
{
    Maemo::Timed::WallClock::Settings s;
    s.setTimeManual(time);
    return setSettings(s);
}

bool DateTimeSettingsPrivate::setSettings(Maemo::Timed::WallClock::Settings &s)
{
    if (!s.check()) {
        return false;
    }

    QDBusPendingCall call = m_timed.wall_clock_settings_async(s);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     this, &DateTimeSettingsPrivate::onWallClockSettingsFinished);

    return true;
}

void DateTimeSettingsPrivate::updateTimedInfo()
{
    QDBusPendingCall call = m_timed.get_wall_clock_info_async();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);

    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     this, &DateTimeSettingsPrivate::onGetWallClockInfoFinished);
}

DateTimeSettings::DateTimeSettings(QObject *parent)
    : QObject(parent)
    , d_ptr(new DateTimeSettingsPrivate(this))
{
}

DateTimeSettings::~DateTimeSettings()
{
}

bool DateTimeSettings::ready() const
{
    return d_ptr->m_timedInfoValid;
}

void DateTimeSettings::setTime(int hour, int minute)
{
    QDate currentDate = QDate::currentDate();
    QTime time(hour, minute);
    QDateTime newTime(currentDate, time);
    d_ptr->setTime(newTime.toTime_t());
}

void DateTimeSettings::setDate(const QDate &date)
{
    QDateTime newTime = QDateTime::currentDateTime();
    newTime.setDate(date);
    d_ptr->setTime(newTime.toTime_t());
}

bool DateTimeSettings::automaticTimeUpdate()
{
    return d_ptr->m_autoSystemTime;
}

void DateTimeSettings::setAutomaticTimeUpdate(bool enable)
{
    if (enable != d_ptr->m_autoSystemTime) {
        Maemo::Timed::WallClock::Settings s;

        if (enable) {
            s.setTimeNitz();
        } else {
            s.setTimeManual();
        }

        d_ptr->setSettings(s);
    }
}

bool DateTimeSettings::automaticTimezoneUpdate()
{
    return d_ptr->m_autoTimezone;
}

void DateTimeSettings::setAutomaticTimezoneUpdate(bool enable)
{
    if (enable != d_ptr->m_autoTimezone) {
        Maemo::Timed::WallClock::Settings s;

        if (enable) {
            s.setTimezoneCellular();
        } else {
            s.setTimezoneManual("");
        }

        d_ptr->setSettings(s);
    }
}

QString DateTimeSettings::timezone() const
{
    return d_ptr->m_timezone;
}

void DateTimeSettings::setTimezone(const QString &tz)
{
    if (tz == d_ptr->m_timezone) {
        return;
    }

    Maemo::Timed::WallClock::Settings s;
    s.setTimezoneManual(tz);
    d_ptr->setSettings(s);
}

void DateTimeSettings::setHourMode(DateTimeSettings::HourMode mode)
{
    Maemo::Timed::WallClock::Settings s;
    s.setFlag24(mode == TwentyFourHours);
    d_ptr->setSettings(s);
}

#include "datetimesettings.moc"
