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

#include "aboutsettings.h"

#include <QDebug>
#include <QStringList>
#include <QNetworkInfo>
#include <QDeviceInfo>
#include <QFile>
#include <QByteArray>
#include <QRegularExpression>
#include <QMap>
#include <QTextStream>
#include <QVariant>
#include <QSettings>

namespace
{

void parseReleaseFile(const QString &filename, QMap<QString, QString> *result)
{
    if (!result->isEmpty()) {
        return;
    }

    // Specification of the format:
    // http://www.freedesktop.org/software/systemd/man/os-release.html

    QFile release(filename);
    if (release.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&release);

        // "All strings should be in UTF-8 format, and non-printable characters
        // should not be used."
        in.setCodec("UTF-8");

        while (!in.atEnd()) {
            QString line = in.readLine();

            // "Lines beginning with "#" shall be ignored as comments."
            if (line.startsWith('#')) {
                continue;
            }

            QString key = line.section('=', 0, 0);
            QString value = line.section('=', 1);

            // Remove trailing whitespace in value
            value = value.trimmed();

            // POSIX.1-2001 says uppercase, digits and underscores.
            //
            // Bash uses "[a-zA-Z_]+[a-zA-Z0-9_]*", so we'll use that too,
            // as we can safely assume that "shell-compatible variable
            // assignments" means it should be compatible with bash.
            //
            // see http://stackoverflow.com/a/2821183
            // and http://stackoverflow.com/a/2821201
            if (!QRegExp("[a-zA-Z_]+[a-zA-Z0-9_]*").exactMatch(key)) {
                qWarning("Invalid key in input line: '%s'", qPrintable(line));
                continue;
            }

            // "Variable assignment values should be enclosed in double or
            // single quotes if they include spaces, semicolons or other
            // special characters outside of A-Z, a-z, 0-9."
            if (((value.at(0) == '\'') || (value.at(0) == '"'))) {
                if (value.at(0) != value.at(value.size() - 1)) {
                    qWarning("Quoting error in input line: '%s'", qPrintable(line));
                    continue;
                }

                // Remove the quotes
                value = value.mid(1, value.size() - 2);
            }

            // "If double or single quotes or backslashes are to be used within
            // variable assignments, they should be escaped with backslashes,
            // following shell style."
            value = value.replace(QRegularExpression("\\\\(.)"), "\\1");

            (*result)[key] = value;
        }

        release.close();
    }
}

}

AboutSettings::AboutSettings(QObject *parent)
:   QObject(parent), m_netinfo(new QNetworkInfo(this)),
    m_devinfo(new QDeviceInfo(this))
{
    QSettings settings(QStringLiteral("/mnt/vendor_data/vendor-data.ini"), QSettings::IniFormat);
    m_vendorName = settings.value(QStringLiteral("Name")).toString();
    m_vendorVersion = settings.value(QStringLiteral("Version")).toString();

    refreshStorageModels();
}

AboutSettings::~AboutSettings()
{
}

qlonglong AboutSettings::totalDiskSpace() const
{
    return m_partitionManager.root().bytesTotal();
}

qlonglong AboutSettings::availableDiskSpace() const
{
    return m_partitionManager.root().bytesAvailable();
}

QVariant AboutSettings::diskUsageModel() const
{
    return m_internalStorage;
}

QVariant AboutSettings::externalStorageUsageModel() const
{
    return m_externalStorage;
}

QString AboutSettings::wlanMacAddress() const
{
    return m_netinfo->macAddress(QNetworkInfo::WlanMode, 0);
}

QString AboutSettings::imei() const
{
    return m_devinfo->imei(0);
}

QString AboutSettings::serial() const
{
    // TODO: eventually we should use QDeviceInfo's uniqueDeviceID()

    QStringList serialFiles;

    serialFiles
        // This file is specific to the Jolla Tablet;
        << "/config/serial/serial.txt"
        // Some devices have serialno in this path.
        << "/sys/firmware/devicetree/base/firmware/android/serialno";

    for (const QString &serialFile : serialFiles) {
        QFile serialTxt(serialFile);
        if (serialTxt.exists() && serialTxt.open(QIODevice::ReadOnly))
            return QString::fromUtf8(serialTxt.readAll()).trimmed();
    }

    return QString();
}

QString AboutSettings::operatingSystemName() const
{
    parseReleaseFile(QStringLiteral("/etc/os-release"), &m_osRelease);

    return m_osRelease["NAME"];
}

QString AboutSettings::softwareVersion() const
{
    parseReleaseFile(QStringLiteral("/etc/os-release"), &m_osRelease);

    return m_osRelease["VERSION"];
}

QString AboutSettings::softwareVersionId() const
{
    parseReleaseFile(QStringLiteral("/etc/os-release"), &m_osRelease);

    return m_osRelease["VERSION_ID"];
}

QString AboutSettings::adaptationVersion() const
{
    parseReleaseFile(QStringLiteral("/etc/hw-release"), &m_hardwareRelease);

    return m_hardwareRelease["VERSION_ID"];
}

QString AboutSettings::vendorName() const
{
    return m_vendorName;
}

QString AboutSettings::vendorVersion() const
{
    return m_vendorVersion;
}

void AboutSettings::refreshStorageModels()
{
    m_internalStorage.clear();
    m_externalStorage.clear();

    m_partitionManager.refresh();

    for (auto partition : m_partitionManager.partitions()) {
        QVariantMap row;
        row[QStringLiteral("mounted")] = partition.status() == Partition::Mounted;
        row[QStringLiteral("path")] = partition.mountPath();
        row[QStringLiteral("available")] = partition.bytesAvailable();
        row[QStringLiteral("total")] = partition.bytesTotal();
        row[QStringLiteral("filesystem")] = partition.filesystemType();
        row[QStringLiteral("devicePath")] = partition.devicePath();
        row[QStringLiteral("storageType")] = [&partition]() {
            switch (partition.storageType()) {
            case Partition::System:
                return QStringLiteral("system");
            case Partition::User:
                return QStringLiteral("user");
            case Partition::Mass:
                return QStringLiteral("mass");
            case Partition::External:
                return QStringLiteral("card");
            default:
                return QString();
            }
        }();

        if (partition.storageType() == Partition::External) {
            m_externalStorage << QVariant(row);
        } else {
            m_internalStorage << QVariant(row);
        }
    }

    emit storageChanged();
}
