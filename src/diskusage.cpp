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
#include <QDebug>
#include <QJSEngine>
#include <QDir>


DiskUsageWorker::DiskUsageWorker(QObject *parent)
    : QObject(parent)
    , m_quit(false)
{
}

DiskUsageWorker::~DiskUsageWorker()
{
}

void DiskUsageWorker::submit(QStringList paths, QJSValue *callback)
{
    emit finished(calculate(paths), callback);
}

QVariantMap DiskUsageWorker::calculate(QStringList paths)
{
    QVariantMap usage;
    // expanded Path places the object in the tree so parents can have it subtracted from its total
    QMap<QString, QString> expandedPaths; // input path -> expanded path
    QMap<QString, QString> originalPaths; // expanded path -> input path

    // Older adaptations (e.g. Jolla 1) don't have /home/.android/. Android home is in the root.
    QString androidHome = QString("/home/.android");
    bool androidHomeExists = QDir(androidHome).exists();

    foreach (const QString &path, paths) {
        QString expandedPath;
        // Pseudo-path for querying RPM database for file sizes
        // ----------------------------------------------------
        // Example path with package name: ":rpm:python3-base"
        // Example path with glob: ":rpm:harbour-*" (will sum up all matching package sizes)
        if (path.startsWith(":rpm:")) {
            QString glob = path.mid(5);
            usage[path] = calculateRpmSize(glob);
            expandedPath = "/usr/" + path;
        } else if (path.startsWith(":apkd:")) {
            // Pseudo-path for querying Android apps' data usage
            QString rest = path.mid(6);
            usage[path] = calculateApkdSize(rest);
            expandedPath = (androidHomeExists ? androidHome : "") + "/data/data";
        } else {
            quint64 size = calculateSize(path, &expandedPath, androidHomeExists);
            if (expandedPath.startsWith(androidHome) && !androidHomeExists) {
                expandedPath = expandedPath.mid(androidHome.length());
            }
            usage[path] = size;
        }

        expandedPaths[path] = expandedPath;
        originalPaths[expandedPath] = path;
        if (m_quit) {
            break;
        }
    }

    // Sort keys in reverse order (so child directories come before their
    // parents, and the calculation is done correctly, no child directory
    // subtracted once too often), for example:
    //  1. a0 = size(/home/<user>/foo/)
    //  2. b0 = size(/home/<user>/)
    //  3. c0 = size(/)
    //
    // This will calculate the following changes in the nested for loop below:
    //  1. b1 = b0 - a0
    //  2. c1 = c0 - a0
    //  3. c2 = c1 - b1
    //
    // Combined and simplified, this will give us the output values:
    //  1. a' = a0
    //  2. b' = b1 = b0 - a0
    //  3. c' = c2 = c1 - b1 = (c0 - a0) - (b0 - a0) = c0 - a0 - b0 + a0 = c0 - b0
    //
    // Or with paths:
    //  1. output(/home/<user>/foo/) = size(/home/<user>/foo/)
    //  2. output(/home/<user>/)     = size(/home/<user>/)     - size(/home/<user>/foo/)
    //  3. output(/)               = size(/)               - size(/home/<user>/)
    QStringList keys;
    foreach (const QString &key, usage.uniqueKeys()) {
        keys << expandedPaths.value(key, key);
    }
    qStableSort(keys.begin(), keys.end(), qGreater<QString>());
    for (int i=0; i<keys.length(); i++) {
        for (int j=i+1; j<keys.length(); j++) {
            QString subpath = keys[i];
            QString path = keys[j];

            if ((subpath.length() > path.length() && subpath.indexOf(path) == 0) || (path == "/")) {
                qlonglong subbytes = usage[originalPaths.value(subpath, subpath)].toLongLong();
                qlonglong bytes = usage[originalPaths.value(path, path)].toLongLong();

                bytes -= subbytes;
                usage[originalPaths.value(path, path)] = bytes;
            }
        }
    }

    return usage;
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
    QThread *m_thread;
    DiskUsageWorker *m_worker;
};

DiskUsagePrivate::DiskUsagePrivate(DiskUsage *usage)
    : q_ptr(usage)
    , m_thread(new QThread())
    , m_worker(new DiskUsageWorker())
{
    m_worker->moveToThread(m_thread);

    QObject::connect(usage, SIGNAL(submit(QStringList, QJSValue *)),
                     m_worker, SLOT(submit(QStringList, QJSValue *)));

    QObject::connect(m_worker, SIGNAL(finished(QVariantMap, QJSValue *)),
                     usage, SLOT(finished(QVariantMap, QJSValue *)));

    QObject::connect(m_thread, SIGNAL(finished()),
                     m_worker, SLOT(deleteLater()));

    QObject::connect(m_thread, SIGNAL(finished()),
                     m_thread, SLOT(deleteLater()));

    m_thread->start();
}

DiskUsagePrivate::~DiskUsagePrivate()
{
    // Make sure the worker quits as soon as possible
    m_worker->scheduleQuit();

    // Tell thread to shut down as early as possible
    m_thread->quit();
}


DiskUsage::DiskUsage(QObject *parent)
    : QObject(parent)
    , d_ptr(new DiskUsagePrivate(this))
    , m_working(false)
{
    qWarning() << Q_FUNC_INFO << "DiskUsage is deprecated in org.nemomobile.systemsettings package 0.5.22 (Sept 2019), use DiskUsage from Nemo.FileManager instead.";
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

    // the result has been set, so emit resultChanged() even if result was not valid
    m_result = usage;
    emit resultChanged();

    setWorking(false);
}

QVariantMap DiskUsage::result() const
{
    return m_result;
}
