
/*
 * Copyright (c) 2015 - 2019 Jolla Ltd.
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#include "ut_diskusage.h"

#include <QtTest>
#include <QDir>

static QVariantMap g_mocked_file_size;
static QVariantMap g_mocked_rpm_size;
static QVariantMap g_mocked_apkd_size;

#define MB(x) ((x) * 1024 * 1024)

#define UT_DISKUSAGE_EXPECT_SIZE(path, size) { \
    QVERIFY(usage.contains(path)); \
    QCOMPARE(usage[path].toLongLong(), size); \
}


/* Mocked implementations of size calculation functions */
quint64 DiskUsageWorker::calculateSize(QString directory, QString *expandedPath, bool)
{
    if (expandedPath) {
        *expandedPath = directory;
    }

    return quint64(g_mocked_file_size.value(directory, qlonglong(0)).toLongLong());
}

quint64 DiskUsageWorker::calculateRpmSize(const QString &glob)
{
    return quint64(g_mocked_rpm_size.value(glob, qlonglong(0)).toLongLong());
}

quint64 DiskUsageWorker::calculateApkdSize(const QString &rest)
{
    return quint64(g_mocked_apkd_size.value(rest, qlonglong(0)).toLongLong());
}


void Ut_DiskUsage::cleanup()
{
    g_mocked_file_size.clear();
    g_mocked_rpm_size.clear();
    g_mocked_apkd_size.clear();
}

void Ut_DiskUsage::testSimple()
{
    g_mocked_file_size["/"] = MB(1000);
    g_mocked_file_size["/home/"] = MB(500);
    g_mocked_file_size["/data/app/"] = MB(100);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() << "/" << "/home/" << "/data/app/");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(400))
    UT_DISKUSAGE_EXPECT_SIZE("/home/", MB(500))
    UT_DISKUSAGE_EXPECT_SIZE("/data/app/", MB(100))
}

void Ut_DiskUsage::testSubtractApkdFromRoot()
{
    g_mocked_file_size["/"] = MB(100);
    g_mocked_apkd_size[""] = MB(20);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() << "/" << ":apkd:");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(80))
    UT_DISKUSAGE_EXPECT_SIZE(":apkd:", MB(20))
}

void Ut_DiskUsage::testSubtractRPMFromRoot()
{
    g_mocked_file_size["/"] = MB(200);
    g_mocked_rpm_size[""] = MB(100);
    g_mocked_rpm_size["harbour-*"] = MB(20);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() << "/" << ":rpm:" << ":rpm:harbour-*");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(100))
    UT_DISKUSAGE_EXPECT_SIZE(":rpm:", MB(80))
    UT_DISKUSAGE_EXPECT_SIZE(":rpm:harbour-*", MB(20))
}

void Ut_DiskUsage::testSubtractSubdirectory()
{
    g_mocked_file_size["/"] = MB(100);
    g_mocked_file_size["/home/"] = MB(50);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() << "/" << "/home/");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(50))
    UT_DISKUSAGE_EXPECT_SIZE("/home/", MB(50))
}

void Ut_DiskUsage::testSubtractNestedSubdirectory()
{
    g_mocked_file_size["/"] = MB(1000);
    g_mocked_file_size["/home/"] = MB(300);
    g_mocked_file_size[QDir::homePath()] = MB(150);
    g_mocked_file_size[QDir::homePath() + "/Documents/"] = MB(70);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() <<
            "/" << "/home/" << QDir::homePath() << QDir::homePath() + "/Documents/");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(1000) - MB(300))
    UT_DISKUSAGE_EXPECT_SIZE("/home/", MB(300) - MB(150))
    UT_DISKUSAGE_EXPECT_SIZE(QDir::homePath(), MB(150) - MB(70))
    UT_DISKUSAGE_EXPECT_SIZE(QDir::homePath() + "/Documents/", MB(70))
}

void Ut_DiskUsage::testSubtractNestedSubdirectoryMulti()
{
    g_mocked_file_size["/"] = MB(1000);
    g_mocked_file_size["/home/"] = MB(300);
    g_mocked_file_size[QDir::homePath()] = MB(150);
    g_mocked_file_size[QDir::homePath() + "/Documents/"] = MB(70);
    g_mocked_file_size["/opt/"] = MB(100);
    g_mocked_file_size["/opt/foo/"] = MB(30);
    g_mocked_file_size["/opt/foo/bar/"] = MB(20);
    g_mocked_file_size["/opt/baz/"] = MB(10);

    QVariantMap usage = DiskUsageWorker().calculate(QStringList() << "/" <<
            "/home/" << QDir::homePath() << QDir::homePath() + "/Documents/" <<
            "/opt/" << "/opt/foo/" << "/opt/foo/bar/" << "/opt/baz/");

    UT_DISKUSAGE_EXPECT_SIZE("/", MB(1000) - MB(300) - MB(100))
    UT_DISKUSAGE_EXPECT_SIZE("/home/", MB(300) - MB(150))
    UT_DISKUSAGE_EXPECT_SIZE(QDir::homePath(), MB(150) - MB(70))
    UT_DISKUSAGE_EXPECT_SIZE(QDir::homePath() + "/Documents/", MB(70))
    UT_DISKUSAGE_EXPECT_SIZE("/opt/", MB(100) - MB(30) - MB(10))
    UT_DISKUSAGE_EXPECT_SIZE("/opt/foo/", MB(30) - MB(20))
    UT_DISKUSAGE_EXPECT_SIZE("/opt/foo/bar/", MB(20))
    UT_DISKUSAGE_EXPECT_SIZE("/opt/baz/", MB(10))
}


QTEST_APPLESS_MAIN(Ut_DiskUsage)
