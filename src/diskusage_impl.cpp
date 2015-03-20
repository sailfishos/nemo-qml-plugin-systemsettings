/*
 * Copyright (C) 2015 Jolla Ltd.
 * Contact: Thomas Perl <thomas.perl@jolla.com>
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

#include "diskusage.h"
#include "diskusage_p.h"

#include <QDir>
#include <QProcess>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

#include <sys/statvfs.h>


quint64 DiskUsageWorker::calculateSize(QString directory, QString *expandedPath)
{
    // In lieu of wordexp(3) support in Qt, fake it
    if (directory.startsWith("~/")) {
        directory = QDir::homePath() + '/' + directory.mid(2);
    }

    if (expandedPath) {
        *expandedPath = directory;
    }

    if (directory == "/") {
        // Shortcut for getting usage of rootfs
        // TODO: Once we have Qt 5.4, use QStorageInfo
        struct statvfs stv;
        memset(&stv, 0, sizeof(stv));
        if (statvfs(directory.toUtf8().constData(), &stv) != 0) {
            // Do not make an entry for the usage here
            qWarning() << "statvfs() failed on:" << directory;
            return 0L;
        }
        quint64 fsSize = float(stv.f_frsize) * float(stv.f_blocks);
        quint64 freeSpace = float(stv.f_frsize) * float(stv.f_bfree);
        return fsSize - freeSpace;
    }

    // "/data/media/" is mounted in "/home/nemo/android_storage/" with read
    // access for the "nemo" user ("/data/media/" itself isn't readable);
    // Mounted via FUSE and /system/bin/sdcard, see here:
    // https://source.android.com/devices/storage/config.html
    if (directory == "/data/media/") {
        directory = "/home/nemo/android_storage/";
    }

    QDir d(directory);
    if (!d.exists() || !d.isReadable()) {
        return 0L;
    }

    QProcess du;
    du.start("du", QStringList() << "-sxb" << directory, QIODevice::ReadOnly);
    du.waitForFinished();
    if (du.exitStatus() != QProcess::NormalExit) {
        qWarning() << "Could not determine size of:" << directory;
        return 0L;
    }
    QStringList size_directory = QString::fromUtf8(du.readAll()).split('\t');

    if (size_directory.size() > 1) {
        return size_directory[0].toULongLong();
    }

    return 0L;
}

quint64 DiskUsageWorker::calculateRpmSize(const QString &glob)
{
    QProcess rpm;
    rpm.start("rpm", QStringList() << "-qa" << "--queryformat=%{name}|%{size}\\n" << glob, QIODevice::ReadOnly);
    rpm.waitForFinished();
    if (rpm.exitStatus() != QProcess::NormalExit) {
        qWarning() << "Could not determine size of RPM packages matching:" << glob;
        return 0L;
    }

    quint64 result = 0L;

    QStringList lines = QString::fromUtf8(rpm.readAll()).split('\n', QString::SkipEmptyParts);
    foreach (const QString &line, lines) {
        int index = line.indexOf('|');
        if (index == -1) {
            qWarning() << "Could not parse RPM output line:" << line;
            continue;
        }

        QString package = line.left(index);
        result += line.mid(index+1).toULongLong();
    }

    return result;
}

quint64 DiskUsageWorker::calculateApkdSize(const QString &rest)
{
    Q_UNUSED(rest)
    QDBusMessage msg = QDBusMessage::createMethodCall("com.jolla.apkd",
            "/com/jolla/apkd", "com.jolla.apkd", "getAndroidAppDataUsage");

    QDBusReply<qulonglong> reply = QDBusConnection::systemBus().call(msg);
    if (reply.isValid()) {
        return quint64(reply.value());
    }

    qWarning() << "Could not determine Android app data usage";
    return 0L;
}
