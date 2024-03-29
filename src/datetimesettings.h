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

#ifndef DATETIMESETTINGS_H
#define DATETIMESETTINGS_H

#include <QObject>
#include <QTime>

#include <systemsettingsglobal.h>
class DateTimeSettingsPrivate;

class SYSTEMSETTINGS_EXPORT DateTimeSettings: public QObject
{
    Q_OBJECT
    Q_ENUMS(HourMode)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool automaticTimeUpdate READ automaticTimeUpdate WRITE setAutomaticTimeUpdate NOTIFY automaticTimeUpdateChanged)
    Q_PROPERTY(bool automaticTimezoneUpdate READ automaticTimezoneUpdate WRITE setAutomaticTimezoneUpdate NOTIFY automaticTimezoneUpdateChanged)
    Q_PROPERTY(QString timezone READ timezone WRITE setTimezone NOTIFY timezoneChanged)

public:
    enum HourMode {
        TwentyFourHours,
        TwelveHours
    };

    explicit DateTimeSettings(QObject *parent = 0);
    virtual ~DateTimeSettings();

    Q_INVOKABLE void setTime(int hour, int minute);
    Q_INVOKABLE void setDate(const QDate &date);

    bool ready() const;

    bool automaticTimeUpdate();
    void setAutomaticTimeUpdate(bool enable);

    bool automaticTimezoneUpdate();
    void setAutomaticTimezoneUpdate(bool enable);

    QString timezone() const;
    void setTimezone(const QString &);

    Q_INVOKABLE void setHourMode(HourMode mode);

signals:
    void readyChanged();
    void timeChanged();
    void automaticTimeUpdateChanged();
    void automaticTimezoneUpdateChanged();
    void timezoneChanged();

private:
    DateTimeSettingsPrivate *d_ptr;
};

#endif
