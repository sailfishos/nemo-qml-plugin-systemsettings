/*
 * Copyright (C) 2017 Jolla Ltd. <martin.jones@jollamobile.com>
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

#ifndef TIMEZONEINFO_H
#define TIMEZONEINFO_H

#include <QByteArray>
#include <QList>

#include <systemsettingsglobal.h>

class TimeZoneInfoPrivate;

class SYSTEMSETTINGS_EXPORT TimeZoneInfo
{
public:
    TimeZoneInfo();
    TimeZoneInfo(const TimeZoneInfo &other);
    ~TimeZoneInfo();

    bool isValid() const;

    QByteArray name() const;
    QByteArray area() const;
    QByteArray city() const;
    QByteArray countryCode() const;
    QByteArray countryName() const;
    QByteArray comments() const;
    qint32 offset() const;

    TimeZoneInfo &operator=(const TimeZoneInfo &other);
    bool operator==(const TimeZoneInfo &other) const;
    bool operator!=(const TimeZoneInfo &other) const;

    static QList<TimeZoneInfo> systemTimeZones();

private:
    friend class TimeZoneInfoPrivate;
    TimeZoneInfoPrivate *d;
};
#endif
