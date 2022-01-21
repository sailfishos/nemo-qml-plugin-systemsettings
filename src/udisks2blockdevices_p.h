/*
 * Copyright (C) 2018 Jolla Ltd. <raine.makelainen@jolla.com>
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

#ifndef UDISKS2_BLOCK_DEVICES_H
#define UDISKS2_BLOCK_DEVICES_H

#include <QMap>
#include <QPointer>
#include <functional>
#include <QDBusObjectPath>

#include "udisks2block_p.h"

class QTimerEvent;

namespace UDisks2 {

class BlockDevices : public QObject
{
    Q_OBJECT
public:
    static BlockDevices* instance();
    virtual ~BlockDevices() override;

    bool contains(const QString &dbusObjectPath) const;
    void remove(const QString &dbusObjectPath);

    Block *device(const QString &dbusObjectPath) const;
    Block *replace(const QString &dbusObjectPath, Block *block);

    void insert(const QString &dbusObjectPath, Block *block);
    Block *find(std::function<bool (const Block *block)> condition);
    Block *find(const QString &devicePath);

    QString objectPath(const QString &devicePath) const;
    QStringList devicePaths(const QStringList &dbusObjectPaths) const;

    bool createBlockDevice(const QString &dbusObjectPath, const InterfacePropertyMap &interfacePropertyMap);
    void createBlockDevices(const QList<QDBusObjectPath> &devices);
    void lock(const QString &dbusObjectPath);

    void waitPartition(Block *block);
    void clearPartitionWait(const QString &dbusObjectPath, bool destroyBlock);

    void removeInterfaces(const QString &dbusObjectPath, const QStringList &interfaces);

    bool hintAuto(const QString &dbusObjectPath);
    bool hintAuto(const Block *maybeHintAuto);

    bool populated() const;

    void dumpBlocks() const;


signals:
    void newBlock(Block *block);
    void externalStoragesPopulated();

private slots:
    void blockCompleted();

private:

    struct PartitionWaiter {
        PartitionWaiter(int timer, Block *block);
        ~PartitionWaiter();

        int timer;
        Block *block;
    };

    BlockDevices(QObject *parent = nullptr);
    Block *doCreateBlockDevice(const QString &dbusObjectPath, const InterfacePropertyMap &interfacePropertyMap);
    void updateFormattingState(Block *block);

    void complete(Block *block, bool forceAccept = false);

    void timerEvent(QTimerEvent *e) override;
    void updatePopulatedCheck();

    QMap<QString, Block *> m_activeBlockDevices;
    QMap<QString, PartitionWaiter*> m_partitionWaits;
    int m_blockCount;
    bool m_populated;

    static QPointer<BlockDevices> sharedInstance;
};

}

#endif
