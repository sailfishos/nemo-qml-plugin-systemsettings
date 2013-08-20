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

#include "languagemodel.h"

#include <QDir>
#include <QDebug>
#include <QSettings>
#include <QHash>
#include <QDBusInterface>

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>


namespace {
const char * const LanguageSupportDirectory = "/usr/share/jolla-supported-languages";

bool nameLessThan(const Language &lang1, const Language &lang2)
{
    return (lang1.name().localeAwareCompare(lang2.name()) <= 0);
}

QString localeConfigPath()
{
    struct passwd *passwdInfo = getpwuid(getuid());
    QString userName;
    if (passwdInfo) {
        userName = passwdInfo->pw_name;
    }
    return QString("/var/lib/environment/%1/locale.conf").arg(userName);
}
}


Language::Language(QString name, QString localeCode, QString region, QString regionLabel)
    : m_name(name), m_localeCode(localeCode), m_region(region), m_regionLabel(regionLabel)
{
}

QString Language::name() const
{
    return m_name;
}

QString Language::localeCode() const
{
    return m_localeCode;
}

QString Language::region() const
{
    return m_region;
}

QString Language::regionLabel() const
{
    return m_regionLabel;
}

LanguageModel::LanguageModel(QObject *parent)
    : QAbstractListModel(parent),
      m_currentIndex(-1)
{
    // get supported languages
    QDir languageDirectory(LanguageSupportDirectory);
    QFileInfoList fileInfoList = languageDirectory.entryInfoList(QStringList("*.conf"), QDir::Files);

    foreach (const QFileInfo &fileInfo, fileInfoList) {
        QSettings settings(fileInfo.filePath(), QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        QString name = settings.value("Name").toString();
        QString localeCode = settings.value("LocaleCode").toString();
        QString region = settings.value("Region").toString();
        //% "Region: %1"
        QString regionLabel = settings.value("RegionLabel", qtTrId("settings_system-la-region")).toString();
        if (name.isEmpty() || localeCode.isEmpty() || region.isEmpty()) {
            continue;
        }
        Language newLanguage(name, localeCode, region, regionLabel);
        m_languages.append(newLanguage);
    }

    qSort(m_languages.begin(), m_languages.end(), nameLessThan);

    readCurrentLocale();
}

LanguageModel::~LanguageModel()
{
}

QHash<int, QByteArray> LanguageModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[LocaleRole] = "locale";
    roles[RegionRole] = "region";
    roles[RegionLabelRole] = "regionLabel";

    return roles;
}

void LanguageModel::readCurrentLocale()
{
    QFile localeConfig(localeConfigPath());

    if (!localeConfig.exists() || !localeConfig.open(QIODevice::ReadOnly)) {
        return;
    }

    QString locale;
    while (!localeConfig.atEnd()) {
        QString line = localeConfig.readLine().trimmed();
        if (line.startsWith("LANG=")) {
             locale = line.mid(5);
             break;
        }
    }

    m_currentIndex = getLocaleIndex(locale);
}



int LanguageModel::rowCount(const QModelIndex & parent) const
{
    Q_UNUSED(parent)
    return m_languages.count();
}

QVariant LanguageModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if (row < 0 || row >= m_languages.count()) {
        return QVariant();
    }

    const Language &language = m_languages.at(row);
    switch (role) {
    case NameRole:
        return language.name();
    case LocaleRole:
        return language.localeCode();
    case RegionRole:
        return language.region();
    case RegionLabelRole:
        return language.regionLabel();
    default:
        return QVariant();
    }
}

int LanguageModel::currentIndex() const
{
    return m_currentIndex;
}

QString LanguageModel::languageName(int index) const
{
    if (index < 0 || index >= m_languages.count()) {
        return QString();
    }
    return m_languages.at(index).name();
}

QString LanguageModel::locale(int index) const
{
    if (index < 0 || index >= m_languages.count()) {
        return QString();
    }
    return m_languages.at(index).localeCode();
}

void LanguageModel::setSystemLocale(const QString &localeCode, LocaleUpdateMode updateMode)
{
    QFile localeConfig(localeConfigPath());
    if (!localeConfig.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "Language model unable to open locale configuration file for writing:" << localeConfigPath()
                   << " - " << localeConfig.errorString();
        return;
    }

    localeConfig.write("# Autogenerated by settings\n");
    localeConfig.write(QString("LANG=%1\n").arg(localeCode).toLatin1());
    localeConfig.close();

    int oldLocale = m_currentIndex;
    m_currentIndex = getLocaleIndex(localeCode);
    if (m_currentIndex != oldLocale) {
        emit currentIndexChanged();
    }

    if (updateMode == UpdateAndReboot) {
        QDBusInterface dsmeInterface("com.nokia.dsme", "/com/nokia/dsme/request", "com.nokia.dsme.request",
                                     QDBusConnection::systemBus());
        dsmeInterface.call("req_reboot");
    }
}

int LanguageModel::getLocaleIndex(const QString &locale) const
{
    int i = 0;
    foreach (Language language, m_languages) {
        if (language.localeCode() == locale) {
            return i;
        }
        i++;
    }

    return -1;
}
