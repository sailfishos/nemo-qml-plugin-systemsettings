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
#include <QStorageInfo>
#include <QNetworkInfo>
#include <QDeviceInfo>
#include <QFile>
#include <QByteArray>
#include <QRegularExpression>
#include <QMap>
#include <QTextStream>
#include <QVariant>

#include <mntent.h>


namespace
{

QMap<QString, QString> parseReleaseFile(const QString &filename)
{
    QMap<QString, QString> result;

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

            result[key] = value;
        }

        release.close();
    }

    return result;
}

struct StorageInfo {
    StorageInfo()
    :   partitionSize(0), external(false)
    { }

    QString mountPath;
    QString devicePath;
    QString filesystem;
    quint64 partitionSize;
    bool external;
};

QMap<QString, StorageInfo> parseExternalPartitions()
{
    QMap<QString, StorageInfo> devices;

    QFile partitions(QStringLiteral("/proc/partitions"));
    if (!partitions.open(QIODevice::ReadOnly))
        return devices;

    // Read file headers
    partitions.readLine();
    partitions.readLine();

    while (!partitions.atEnd()) {
        QByteArray line = partitions.readLine().trimmed();

        int nameIndex = line.lastIndexOf(' ');
        if (nameIndex <= 0)
            continue;

        int sizeIndex = line.lastIndexOf(' ', nameIndex - 1);
        if (sizeIndex == -1)
            continue;

        QByteArray size = line.mid(sizeIndex+1, nameIndex - sizeIndex - 1);
        QByteArray name = line.mid(nameIndex+1);

        if (name.startsWith("mmcblk1")) {
            // If adding a partition remove the whole device.
            devices.remove(QStringLiteral("/dev/mmcblk1"));

            StorageInfo info;
            info.devicePath = QStringLiteral("/dev/") + QString::fromLatin1(name);
            info.partitionSize = size.toULongLong() * 1024;
            info.external = true;

            devices.insert(info.devicePath, info);
        }
    }

    return devices;
}

}

AboutSettings::AboutSettings(QObject *parent)
:   QObject(parent), m_sysinfo(new QStorageInfo(this)), m_netinfo(new QNetworkInfo(this)),
    m_devinfo(new QDeviceInfo(this))
{
    refreshStorageModels();
}

AboutSettings::~AboutSettings()
{
}

qlonglong AboutSettings::totalDiskSpace() const
{
    return m_sysinfo->totalDiskSpace("/");
}

qlonglong AboutSettings::availableDiskSpace() const
{
    return m_sysinfo->availableDiskSpace("/");
}

QVariant AboutSettings::diskUsageModel() const
{
    return m_internalStorage;
}

QVariant AboutSettings::externalStorageUsageModel() const
{
    return m_externalStorage;
}

QString AboutSettings::bluetoothAddress() const
{
    return m_netinfo->macAddress(QNetworkInfo::BluetoothMode, 0);
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
    // XXX: For now, this is specific to the Jolla Tablet; eventually we should
    // use QDeviceInfo's uniqueDeviceID(), but that does not always return the
    // serial number, so this is our best bet for the short term (this will not
    // show any serial number on the Phone, there we have the IMEI instead).

    QFile serial_txt("/config/serial/serial.txt");
    if (serial_txt.exists()) {
        serial_txt.open(QIODevice::ReadOnly);
        return QString::fromUtf8(serial_txt.readAll()).trimmed();
    } else {
        return "";
    }
}

QString AboutSettings::softwareVersion() const
{
    return parseReleaseFile("/etc/os-release")["VERSION"];
}

QString AboutSettings::adaptationVersion() const
{
    return parseReleaseFile("/etc/hw-release")["VERSION_ID"];
}

void AboutSettings::refreshStorageModels()
{
    // Optional mountpoints that we want to report disk usage for
    QStringList candidates;
    candidates << QStringLiteral("/home");

    QMap<QString, StorageInfo> devices = parseExternalPartitions();

    FILE *fsd = setmntent(_PATH_MOUNTED, "r");
    struct mntent entry;
    char buffer[3*PATH_MAX];
    while ((getmntent_r(fsd, &entry, buffer, sizeof(buffer))) != NULL) {
        StorageInfo info;
        info.mountPath = QString::fromLatin1(entry.mnt_dir);
        info.devicePath = QString::fromLatin1(entry.mnt_fsname);
        info.filesystem = QString::fromLatin1(entry.mnt_type);

        if (info.mountPath == QLatin1String("/") && info.devicePath.startsWith(QLatin1Char('/'))) {
            // Always report rootfs, replacing other mounts from same device
            devices.insert(info.devicePath, info);
        } else if (!devices.contains(info.devicePath) || devices.value(info.devicePath).external) {
            // Optional candidates and external storage
            if (candidates.contains(info.mountPath)) {
                devices.insert(info.devicePath, info);
            } else if (info.devicePath.startsWith(QLatin1String("/dev/mmcblk1"))) {
                info.external = true;
                devices.insert(info.devicePath, info);
            }
        }
    }
    endmntent(fsd);

    m_internalStorage.clear();
    m_externalStorage.clear();

    foreach (const StorageInfo &info, devices) {
        if (!info.external) {
            QVariantMap row;

            row[QStringLiteral("storageType")] = info.mountPath == QLatin1String("/")
                                                 ? QStringLiteral("system")
                                                 : QStringLiteral("user");

            row[QStringLiteral("path")] = info.mountPath;
            row[QStringLiteral("mounted")] = true;
            row[QStringLiteral("available")] = m_sysinfo->availableDiskSpace(info.mountPath);
            row[QStringLiteral("total")] = m_sysinfo->totalDiskSpace(info.mountPath);
            row[QStringLiteral("filesystem")] = info.filesystem;
            row[QStringLiteral("devicePath")] = info.devicePath;

            m_internalStorage << QVariant(row);
        } else {
            QVariantMap row;

            bool mounted = !info.mountPath.isEmpty();

            row[QStringLiteral("storageType")] = QStringLiteral("card");
            row[QStringLiteral("mounted")] = mounted;
            row[QStringLiteral("path")] = info.mountPath;
            row[QStringLiteral("available")] = mounted ? m_sysinfo->availableDiskSpace(info.mountPath) : 0;
            row[QStringLiteral("total")] = mounted ? m_sysinfo->totalDiskSpace(info.mountPath) : info.partitionSize;
            row[QStringLiteral("filesystem")] = info.filesystem;
            row[QStringLiteral("devicePath")] = info.devicePath;

            m_externalStorage << QVariant(row);
        }
    }

    emit storageChanged();
}
