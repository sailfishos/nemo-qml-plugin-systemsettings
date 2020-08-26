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

#include "udisks2blockdevices_p.h"
#include "partitionmanager_p.h"
#include "logging_p.h"

#include <QRegularExpression>
#include <QTimerEvent>

#include <QDebug>

#define PARTITION_WAIT_TIMEOUT 3000

using namespace UDisks2;

QPointer<BlockDevices> BlockDevices::sharedInstance = nullptr;

BlockDevices *BlockDevices::instance()
{
    return sharedInstance ? sharedInstance.data() : new BlockDevices;
}

BlockDevices::~BlockDevices()
{
    qDeleteAll(m_blockDevices);
    qDeleteAll(m_partitionWaits);

    m_blockDevices.clear();
    m_partitionWaits.clear();
    sharedInstance = nullptr;
}

bool BlockDevices::contains(const QString &dbusObjectPath) const
{
    return m_blockDevices.contains(dbusObjectPath);
}

void BlockDevices::remove(const QString &dbusObjectPath)
{
    if (contains(dbusObjectPath)) {
        Block *block = m_blockDevices.take(dbusObjectPath);
        clearPartitionWait(dbusObjectPath, false);
        delete block;
    }
}

Block *BlockDevices::device(const QString &dbusObjectPath) const
{
    return m_blockDevices.value(dbusObjectPath, nullptr);
}

Block *BlockDevices::replace(const QString &dbusObjectPath, Block *block)
{
    Block *deviceReplace = device(dbusObjectPath);

    // Clear partition wait before morphing a block.
    if (m_partitionWaits.contains(deviceReplace->partitionTable())) {
        clearPartitionWait(deviceReplace->partitionTable(), false);
    }

    deviceReplace->morph(*block);
    m_blockDevices.remove(dbusObjectPath);
    insert(deviceReplace->path(), deviceReplace);
    block->deleteLater();
    return deviceReplace;
}

void BlockDevices::insert(const QString &dbusObjectPath, Block *block)
{
    m_blockDevices.insert(dbusObjectPath, block);
}

Block *BlockDevices::find(std::function<bool (const Block *)> condition)
{
    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
        Block *block = i.value();
        if (condition(block)) {
            return block;
        }
    }
    return nullptr;
}

Block *BlockDevices::find(const QString &devicePath)
{
    return find([devicePath](const Block *block){
        return block->device() == devicePath || block->cryptoBackingDevicePath() == devicePath;
    });
}

QString BlockDevices::objectPath(const QString &devicePath) const
{
    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
        Block *block = i.value();
        if (block->device() == devicePath) {
            return block->path();
        } else if (block->cryptoBackingDevicePath() == devicePath) {
            return block->cryptoBackingDeviceObjectPath();
        }
    }

    return QString();
}

QStringList BlockDevices::devicePaths(const QStringList &dbusObjectPaths) const
{
    QStringList paths;
    for (const QString &objectPath : dbusObjectPaths) {
        for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
            Block *block = i.value();
            if (block->path() == objectPath || block->cryptoBackingDeviceObjectPath() == objectPath) {
                paths << block->device();
            }
        }
    }
    return paths;
}

bool BlockDevices::createBlockDevice(const QString &dbusObjectPath, const InterfacePropertyMap &interfacePropertyMap)
{
    if (!BlockDevices::isExternal(dbusObjectPath)) {
        updatePopulatedCheck();
        return false;
    }

    return doCreateBlockDevice(dbusObjectPath, interfacePropertyMap);
}

void BlockDevices::createBlockDevices(const QList<QDBusObjectPath> &devices)
{
    m_blockCount = devices.count();
    if (m_blockCount == 0) {
        m_populated = true;
        emit externalStoragesPopulated();
    }

    for (const QDBusObjectPath &dbusObjectPath : devices) {
        createBlockDevice(dbusObjectPath.path(), UDisks2::InterfacePropertyMap());
    }
}

void BlockDevices::lock(const QString &dbusObjectPath)
{
    Block *deviceMapped = find([dbusObjectPath](const Block *block) {
        return block->cryptoBackingDeviceObjectPath() == dbusObjectPath;
    });

    if (deviceMapped && (deviceMapped->isLocking() || deviceMapped->isFormatting())) {
        Block *newBlock = doCreateBlockDevice(dbusObjectPath, InterfacePropertyMap());
        if (newBlock && deviceMapped->isFormatting()) {
            newBlock->setFormatting(true);
        }
    }
}

void BlockDevices::waitPartition(Block *block)
{
    m_partitionWaits.insert(block->path(), new PartitionWaiter(startTimer(PARTITION_WAIT_TIMEOUT), block));
}

void BlockDevices::clearPartitionWait(const QString &dbusObjectPath, bool destroyBlock)
{
    PartitionWaiter *waiter = m_partitionWaits.value(dbusObjectPath,  nullptr);
    if (waiter) {
        killTimer(waiter->timer);

        // Just nullify block and let it live.
        if (!destroyBlock) {
            waiter->block = nullptr;
        }

        delete waiter;
    }

    m_partitionWaits.remove(dbusObjectPath);
}

void BlockDevices::removeInterfaces(const QString &dbusObjectPath, const QStringList &interfaces)
{
    clearPartitionWait(dbusObjectPath, false);

    UDisks2::Block *block = device(dbusObjectPath);
    if (block) {
        if (interfaces.contains(UDISKS2_FILESYSTEM_INTERFACE)) {
            block->removeInterface(UDISKS2_FILESYSTEM_INTERFACE);
        }
        if (interfaces.contains(UDISKS2_ENCRYPTED_INTERFACE)) {
            block->removeInterface(UDISKS2_ENCRYPTED_INTERFACE);
        }

        if (interfaces.contains(UDISKS2_BLOCK_INTERFACE)) {
            delete block;
            m_blockDevices.remove(dbusObjectPath);
        }
    }
}

bool BlockDevices::populated() const
{
    return m_populated;
}

bool BlockDevices::isExternal(const QString &dbusObjectPath)
{
    static const QRegularExpression externalBlockDevice(QStringLiteral("^/org/freedesktop/UDisks2/block_devices/%1$").arg(externalDevice));
    return externalBlockDevice.match(dbusObjectPath).hasMatch();
}

void BlockDevices::blockCompleted()
{
    Block *completedBlock = qobject_cast<Block *>(sender());
    if (completedBlock->isValid() && (completedBlock->isPartitionTable() ||
                                      (completedBlock->hasInterface(UDISKS2_BLOCK_INTERFACE) && completedBlock->interfaceCount() == 1)) ){
        qCInfo(lcMemoryCardLog) << "Start waiting for block" << completedBlock->device();
        waitPartition(completedBlock);
        updatePopulatedCheck();
        return;
    }

    clearPartitionWait(completedBlock->partitionTable(), true);
    complete(completedBlock);

    // Check only after complete has been called.
    updatePopulatedCheck();
}

BlockDevices::BlockDevices(QObject *parent)
    : QObject(parent)
    , m_blockCount(0)
    , m_populated(false)
{
    Q_ASSERT(!sharedInstance);

    sharedInstance = this;
}

Block *BlockDevices::doCreateBlockDevice(const QString &dbusObjectPath, const InterfacePropertyMap &interfacePropertyMap)
{
    if (contains(dbusObjectPath)) {
        Block *block = device(dbusObjectPath);
        if (block && interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE)) {
            block->addInterface(UDISKS2_FILESYSTEM_INTERFACE, interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE));
        }
        if (block && interfacePropertyMap.contains(UDISKS2_ENCRYPTED_INTERFACE)) {
            block->addInterface(UDISKS2_ENCRYPTED_INTERFACE, interfacePropertyMap.value(UDISKS2_ENCRYPTED_INTERFACE));
        }

        return block;
    }

    Block *block = new Block(dbusObjectPath, interfacePropertyMap);
    updateFormattingState(block);
    connect(block, &Block::completed, this, &BlockDevices::blockCompleted);
    return block;
}

void BlockDevices::updateFormattingState(Block *block)
{
    Block *cryptoBackingDevice = nullptr;
    QString cryptoBackingDevicePath = block->cryptoBackingDeviceObjectPath();

    // If we have crypto backing device, copy over formatting state.
    if (cryptoBackingDevicePath != QLatin1String("/") && (cryptoBackingDevice = m_blockDevices.value(cryptoBackingDevicePath, nullptr))) {
        block->blockSignals(true);
        block->setFormatting(cryptoBackingDevice->isFormatting());
        block->blockSignals(false);
    }
}

void BlockDevices::dumpBlocks() const
{
    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
        i.value()->dumpInfo();
    }
}

void BlockDevices::complete(Block *block, bool forceAccept)
{
    if (!block->isExternal()) {
        block->deleteLater();
        return;
    }

    // Check if device is already unlocked.
    Block *unlocked = nullptr;
    if (block->isEncrypted()) {
        QString newPath = block->path();
        unlocked = find([newPath](const Block *block) {
            return block->cryptoBackingDeviceObjectPath() == newPath && !block->isLocking();
        });
    }

    bool willAccept = !unlocked && (block->isPartition() || block->isMountable() || block->isEncrypted() || block->isFormatting() || forceAccept);
    qCInfo(lcMemoryCardLog) << "Completed block" << qPrintable(block->path())
                            << "is" << (willAccept ? "accepted" : block->isPartition() ? "kept" : "rejected");
    block->dumpInfo();

    if (willAccept) {
        // Hope that somebody will handle this signal and call insert()
        // to add this block to m_blockDevices.
        emit newBlock(block);
    } else if (block->isPartition()) {
        // Silently keep partitions around so that we can filter out
        // partition tables in timerEvent().
        insert(block->path(), block);
    } else {
        // This is garbage block device that should not be exposed
        // from the partition model.
        block->removeInterface(UDISKS2_BLOCK_INTERFACE);
        block->deleteLater();
    }
}

void BlockDevices::timerEvent(QTimerEvent *e)
{
    for (QMap<QString, PartitionWaiter *>::iterator i = m_partitionWaits.begin(); i != m_partitionWaits.end(); ++i) {
        PartitionWaiter *waiter = i.value();
        int timerId = waiter->timer;
        if (e->timerId() == timerId) {
            QString path = i.key();
            qCDebug(lcMemoryCardLog) << "Waiting partitions:" << m_partitionWaits.keys() << path;
            dumpBlocks();

            Block *partitionTable = find([path](const Block *block) {
                return block->partitionTable() == path;
            });

            // No partition found that would be part of this partion table. Accept this one.
            if (!partitionTable) {
                complete(waiter->block, true);
            }
            clearPartitionWait(path, partitionTable);
            break;
        }
    }
}

void BlockDevices::updatePopulatedCheck()
{
    if (!m_populated) {
        --m_blockCount;
        if (m_blockCount == 0) {
            m_populated = true;
            emit externalStoragesPopulated();
        }
    }
}

BlockDevices::PartitionWaiter::PartitionWaiter(int timer, Block *block)
    : timer(timer)
    , block(block)
{
}

BlockDevices::PartitionWaiter::~PartitionWaiter()
{
    delete block;
    block = nullptr;
    timer = 0;
}
