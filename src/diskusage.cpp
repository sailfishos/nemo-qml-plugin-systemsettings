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

#include <QThread>
#include <QDir>
#include <QProcess>
#include <QDebug>
#include <QJSEngine>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>

#include <sys/statvfs.h>

DiskUsageWorker::DiskUsageWorker(QObject *parent)
    : QObject(parent)
    , m_quit(false)
{
}

DiskUsageWorker::~DiskUsageWorker()
{
}

static quint64 calculateSize(QString directory)
{
    // In lieu of wordexp(3) support in Qt, fake it
    if (directory.startsWith("~/")) {
        directory = QDir::homePath() + '/' + directory.mid(2);
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

static quint64 calculateRpmSize(const QString &glob)
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

static quint64 calculateApkdSize(const QString &rest)
{
    QDBusMessage msg = QDBusMessage::createMethodCall("com.jolla.apkd",
            "/com/jolla/apkd", "com.jolla.apkd", "getAndroidAppDataUsage");

    QDBusReply<qulonglong> reply = QDBusConnection::systemBus().call(msg);
    if (reply.isValid()) {
        return quint64(reply.value());
    }

    qWarning() << "Could not determine Android app data usage";
    return 0L;
}

void DiskUsageWorker::submit(QStringList paths, QJSValue *callback)
{
    QVariantMap usage;

    foreach (const QString &path, paths) {
        // Pseudo-path for querying RPM database for file sizes
        // ----------------------------------------------------
        // Example path with package name: ":rpm:python3-base"
        // Example path with glob: ":rpm:harbour-*" (will sum up all matching package sizes)
        if (path.startsWith(":rpm:")) {
            QString glob = path.mid(5);
            usage[path] = calculateRpmSize(glob);
        } else if (path.startsWith(":apkd:")) {
            // Pseudo-path for querying Android apps' data usage
            QString rest = path.mid(6);
            usage[path] = calculateApkdSize(rest);
        } else if (path == "/") {
            // Shortcut for getting usage of rootfs
            // TODO: Once we have Qt 5.4, use QStorageInfo
            struct statvfs stv;
            memset(&stv, 0, sizeof(stv));
            if (statvfs(path.toUtf8().constData(), &stv) != 0) {
                // Do not make an entry for the usage here
                qWarning() << "statvfs() failed on:" << path;
                continue;
            }
            usage[path] = quint64(float(stv.f_frsize) * float(stv.f_blocks));
        } else {
            usage[path] = calculateSize(path);
        }

        if (m_quit) {
            break;
        }
    }

    for (QVariantMap::iterator it = usage.begin(); it != usage.end(); ++it) {
        const QString &path = it.key();
        qlonglong bytes = it.value().toLongLong();

        for (QVariantMap::const_iterator it2 = usage.cbegin(); it2 != usage.cend(); ++it2) {
            const QString &subpath = it2.key();
            const qlonglong subbytes = it2.value().toLongLong();

            if ((subpath.length() > path.length() && subpath.indexOf(path) == 0) ||
                    (path == "/" && subpath.startsWith(":"))) {
                bytes -= subbytes;
            }
        }

        if (it.value() != bytes) {
            it.value() = bytes;
        }
    }

    emit finished(usage, callback);
}


class DiskUsagePrivate
{
    Q_DISABLE_COPY(DiskUsagePrivate)
    Q_DECLARE_PUBLIC(DiskUsage)

    DiskUsage * const q_ptr;

public:
    DiskUsagePrivate(DiskUsage *usage);
    ~DiskUsagePrivate();

private:
    QThread m_thread;
    DiskUsageWorker *m_worker;
};

DiskUsagePrivate::DiskUsagePrivate(DiskUsage *usage)
    : q_ptr(usage)
    , m_thread()
    , m_worker(new DiskUsageWorker())
{
    m_worker->moveToThread(&m_thread);

    QObject::connect(usage, SIGNAL(submit(QStringList, QJSValue *)),
                     m_worker, SLOT(submit(QStringList, QJSValue *)));

    QObject::connect(m_worker, SIGNAL(finished(QVariantMap, QJSValue *)),
                     usage, SLOT(finished(QVariantMap, QJSValue *)));

    m_thread.start();
}

DiskUsagePrivate::~DiskUsagePrivate()
{
    // Make sure the worker quits as soon as possible
    m_worker->scheduleQuit();

    // Wait for thread to shut down
    m_thread.quit();
    if (!m_thread.wait(10 * 1000)) {
        qWarning("Worker thread did not quit in time");
    }

    m_worker->deleteLater();
}


DiskUsage::DiskUsage(QObject *parent)
    : QObject(parent)
    , d_ptr(new DiskUsagePrivate(this))
    , m_working(false)
{
}

DiskUsage::~DiskUsage()
{
}

void DiskUsage::calculate(const QStringList &paths, QJSValue callback)
{
    QJSValue *cb = 0;

    if (!callback.isNull() && !callback.isUndefined() && callback.isCallable()) {
        cb = new QJSValue(callback);
    }

    setWorking(true);
    emit submit(paths, cb);
}

void DiskUsage::finished(QVariantMap usage, QJSValue *callback)
{
    if (callback) {
        callback->call(QJSValueList() << callback->engine()->toScriptValue(usage));
        delete callback;
    }

    setWorking(false);
}
