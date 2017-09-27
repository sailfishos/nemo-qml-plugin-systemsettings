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

#include "timezoneinfo.h"

#include <sys/time.h>

#include <QDebug>
#include <QFile>
#include <QDataStream>

namespace {

static const QString ZoneInfoPath = QStringLiteral("/usr/share/zoneinfo/");

static QByteArray scanWord(const char *&ch)
{
    const char *start = ch;
    while (*ch && !isspace(*ch))
        ++ch;

    return QByteArray(start, ch-start);
}

static QByteArray scanToEnd(const char *&ch)
{
    const char *start = ch;
    while (*ch && *ch != '\n')
        ++ch;

    return QByteArray(start, ch-start);
}

static void skipSpace(const char *&ch)
{
    while (*ch && isspace(*ch))
        ++ch;
}

static QHash<QByteArray,QByteArray> parseIso3166()
{
    QHash<QByteArray,QByteArray> countries;
    QFile file(ZoneInfoPath + QStringLiteral("iso3166.tab"));

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open timezone file:" << file.fileName();
        return countries;
    }

    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        if (line.length() == 0)
            break;
        if (line[0] == '#')
            continue;

        const char *ch = line.data();
        QByteArray code = scanWord(ch);
        if (!code.isEmpty()) {
            skipSpace(ch);
            countries.insert(code, scanToEnd(ch));
        }
    }

    return countries;
}

}

class TimeZoneInfoPrivate
{
public:
    TimeZoneInfoPrivate();
    ~TimeZoneInfoPrivate();

    static QList<TimeZoneInfo> parseZoneTab();
    static void parseZoneTabLine(const QByteArray &line, TimeZoneInfo *tzInfo);
    static void parseZoneInfo(TimeZoneInfo *tzInfo);

    QByteArray name;
    QByteArray area;
    QByteArray city;
    QByteArray countryCode;
    QByteArray countryName;
    QByteArray comments;
    qint32 offset;
    bool valid;
};

TimeZoneInfoPrivate::TimeZoneInfoPrivate()
    : offset(0)
    , valid(false)
{
}

TimeZoneInfoPrivate::~TimeZoneInfoPrivate()
{
}

QList<TimeZoneInfo> TimeZoneInfoPrivate::parseZoneTab()
{
    QList<TimeZoneInfo> timeZones;
    QHash<QByteArray,QByteArray> countries = parseIso3166();

    QFile file(ZoneInfoPath + QStringLiteral("zone.tab"));

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open timezone file:" << file.fileName();
        return timeZones;
    }

    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        if (line.length() == 0)
            break;
        if (line[0] == '#')
            continue;

        TimeZoneInfo tz;
        parseZoneTabLine(line, &tz);
        parseZoneInfo(&tz);
        if (tz.isValid()) {
            tz.d->countryName = countries.value(tz.d->countryCode);
            timeZones.append(tz);
        }
    }

    return timeZones;
}

void TimeZoneInfoPrivate::parseZoneTabLine(const QByteArray &line, TimeZoneInfo *tzInfo)
{
    if (!tzInfo) {
        return;
    }
    int column = 0;
    const char *ch = line.data();
    while (*ch && *ch != '\n') {
        switch (column) {
        case 0:
            tzInfo->d->countryCode = scanWord(ch);
            skipSpace(ch);
            break;
        case 1:
            // location
            scanWord(ch);
            skipSpace(ch);
            break;
        case 2:
            tzInfo->d->name = scanWord(ch);
            skipSpace(ch);
            break;
        case 3:
            tzInfo->d->comments = scanToEnd(ch);
            break;
        }
        ++column;
    }
    tzInfo->d->valid = column > 2;

    if (tzInfo->d->valid) {
        int slash = tzInfo->d->name.lastIndexOf('/');
        if (slash > 0) {
            tzInfo->d->area = tzInfo->d->name.left(slash);
            tzInfo->d->city = tzInfo->d->name.mid(slash+1);
        }
    }
}

void TimeZoneInfoPrivate::parseZoneInfo(TimeZoneInfo *tzInfo)
{
    if (!tzInfo) {
        return;
    }

    QFile file(ZoneInfoPath + tzInfo->d->name);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open timezone file:" << file.fileName();
        tzInfo->d->valid = false;
        return;
    }

    QByteArray data = file.readAll();
    if (data.count() < 46 || !data.startsWith("TZif")) {
        qWarning() << "Invalid timezone file:" << file.fileName();
        tzInfo->d->valid = false;
        return;
    }

    QDataStream ds(data);
    ds.skipRawData(20); // 'TZif' plus version char plus 15 unused chars

    // read header
    qint32 tzh_ttisgmtcnt, tzh_ttisstdcnt, tzh_leapcnt, tzh_timecnt, tzh_typecnt, tzh_charcnt;
    ds >> tzh_ttisgmtcnt >> tzh_ttisstdcnt >> tzh_leapcnt >> tzh_timecnt >> tzh_typecnt >> tzh_charcnt;

    // followed by tzh_timecnt four-byte values of type long
    qint32 tmp32;
    for (int i = 0; i < tzh_timecnt; ++i)
        ds >> tmp32;

    uchar tmp8 = 0;
    QVarLengthArray<int, 128> transitionIndexes(tzh_timecnt);
    // Next come  tzh_timecnt  one-byte  values of type unsigned  char
    for (int i = 0; i < tzh_timecnt; ++i) {
        ds >> tmp8;
        transitionIndexes[i] = tmp8;
    }

    // then we have tzh_typecnt ttinfo structures
    QVector<QPair<qint32,int> > types(tzh_typecnt);
    for (int i = 0; i < tzh_typecnt; ++i) {
        ds >> tmp32; // tt_gmtoff
        ds >> tmp8;  // tt_isdst
        types[i].first = tmp32;
        types[i].second = tmp8;
        ds >> tmp8;  // tt_abbrind
    }

    if (tzh_typecnt)
        tzInfo->d->offset = types[0].first;

    if (tzh_timecnt) {
        // find the last non-dst transition
        int i = tzh_timecnt - 1;
        while (i >= 0 && types[transitionIndexes[i]].second) {
            --i;
        }
        if (i >= 0)
            tzInfo->d->offset = types[transitionIndexes[i]].first;
    }

    // ignore the rest for now
}


TimeZoneInfo::TimeZoneInfo()
    : d(new TimeZoneInfoPrivate)
{
}

TimeZoneInfo::~TimeZoneInfo()
{
    delete d;
}

TimeZoneInfo::TimeZoneInfo(const TimeZoneInfo &other)
    : d(new TimeZoneInfoPrivate)
{
    operator=(other);
}

bool TimeZoneInfo::isValid() const
{
    return d->valid;
}

QByteArray TimeZoneInfo::countryCode() const
{
    return d->countryCode;
}

QByteArray TimeZoneInfo::countryName() const
{
    return d->countryName;
}

QByteArray TimeZoneInfo::name() const
{
    return d->name;
}

QByteArray TimeZoneInfo::area() const
{
    return d->area;
}

QByteArray TimeZoneInfo::city() const
{
    return d->city;
}

QByteArray TimeZoneInfo::comments() const
{
    return d->comments;
}

qint32 TimeZoneInfo::offset() const
{
    return d->offset;
}

TimeZoneInfo &TimeZoneInfo::operator=(const TimeZoneInfo &other)
{
    if (this == &other) {
        return *this;
    }

    d->name = other.d->name;
    d->area = other.d->area;
    d->city = other.d->city;
    d->countryCode = other.d->countryCode;
    d->countryName = other.d->countryName;
    d->comments = other.d->comments;
    d->offset = other.d->offset;
    d->valid = other.d->valid;

    return *this;
}

bool TimeZoneInfo::operator==(const TimeZoneInfo &other) const
{
    return d->name == other.d->name;
}

bool TimeZoneInfo::operator!=(const TimeZoneInfo &other) const
{
    return d->name != other.d->name;
}

QList<TimeZoneInfo> TimeZoneInfo::systemTimeZones()
{
    return TimeZoneInfoPrivate::parseZoneTab();
}
