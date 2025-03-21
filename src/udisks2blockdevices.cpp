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
    qDeleteAll(m_activeBlockDevices);
    qDeleteAll(m_partitionWaits);

    m_activeBlockDevices.clear();
    m_partitionWaits.clear();
    sharedInstance = nullptr;
}

bool BlockDevices::contains(const QString &dbusObjectPath) const
{
    return m_activeBlockDevices.contains(dbusObjectPath);
}

void BlockDevices::remove(const QString &dbusObjectPath)
{
    if (contains(dbusObjectPath)) {
        Block *block = m_blockDevices.take(dbusObjectPath);
        m_activeBlockDevices.remove(dbusObjectPath);
        clearPartitionWait(dbusObjectPath, false);
        delete block;
    }
}

Block *BlockDevices::device(const QString &dbusObjectPath) const
{
    Block *block = m_activeBlockDevices.value(dbusObjectPath, nullptr);
    if (block) {
        return block;
    }
    return m_blockDevices.value(dbusObjectPath, nullptr);
}

void BlockDevices::deactivate(const QString &dbusObjectPath)
{
    m_activeBlockDevices.remove(dbusObjectPath);
}

void BlockDevices::insert(const QString &dbusObjectPath, Block *block)
{
    m_activeBlockDevices.insert(dbusObjectPath, block);
}

Block *BlockDevices::find(std::function<bool (const Block *)> condition)
{
    for (QMap<QString, Block *>::const_iterator i = m_activeBlockDevices.constBegin(); i != m_activeBlockDevices.constEnd(); ++i) {
        Block *block = i.value();
        if (condition(block)) {
            return block;
        }
    }

    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
        Block *block = i.value();
        if (condition(block)) {
            return block;
        }
    }

    for (QMap<QString, Block *>::const_iterator i = m_pendingBlockDevices.constBegin(); i != m_pendingBlockDevices.constEnd(); ++i) {
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
    for (QMap<QString, Block *>::const_iterator i = m_activeBlockDevices.constBegin(); i != m_activeBlockDevices.constEnd(); ++i) {
        Block *block = i.value();
        if (block->device() == devicePath) {
            return block->path();
        } else if (block->cryptoBackingDevicePath() == devicePath) {
            return block->cryptoBackingDeviceObjectPath();
        }
    }

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
        for (QMap<QString, Block *>::const_iterator i = m_activeBlockDevices.constBegin(); i != m_activeBlockDevices.constEnd(); ++i) {
            Block *block = i.value();
            if (block->path() == objectPath || block->cryptoBackingDeviceObjectPath() == objectPath) {
                paths << block->device();
            }
        }

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
    return doCreateBlockDevice(dbusObjectPath, interfacePropertyMap);
}

void BlockDevices::createBlockDevices(const QList<QDBusObjectPath> &devices)
{
    m_blockCount = devices.count();
    updatePopulatedCheck();

    for (const QDBusObjectPath &dbusObjectPath : devices) {
        createBlockDevice(dbusObjectPath.path(), UDisks2::InterfacePropertyMap());
    }
}

void BlockDevices::lock(const QString &dbusObjectPath)
{
    Block *newActive = m_blockDevices.value(dbusObjectPath, nullptr);

    if (newActive) {
        emit newBlock(newActive, true);
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

        if (interfaces.contains(UDISKS2_BLOCK_INTERFACE)) {
            delete block;
            m_activeBlockDevices.remove(dbusObjectPath);
            m_blockDevices.remove(dbusObjectPath);
        } else {
            if (interfaces.contains(UDISKS2_FILESYSTEM_INTERFACE)) {
                block->removeInterface(UDISKS2_FILESYSTEM_INTERFACE);
            }
            if (interfaces.contains(UDISKS2_ENCRYPTED_INTERFACE)) {
                block->removeInterface(UDISKS2_ENCRYPTED_INTERFACE);
            }
        }
    }
}

bool BlockDevices::populated() const
{
    return m_populated;
}

bool BlockDevices::hintAuto(const Block *maybeHintAuto)
{
    if (!maybeHintAuto->hintAuto()) {
        if (!maybeHintAuto->hasCryptoBackingDevice())
            return false;
        return hintAuto(maybeHintAuto->cryptoBackingDeviceObjectPath());
    }
    return true;
}

bool BlockDevices::hintAuto(const QString &devicePath)
{
    Block *maybeHintAuto = find([devicePath](const Block *block) {
        return block->device() == devicePath || block->path() == devicePath;
    });
    if (!maybeHintAuto)
        return false;

    return hintAuto(maybeHintAuto);
}

void BlockDevices::blockCompleted()
{
    Block *completedBlock = qobject_cast<Block *>(sender());
    if (completedBlock->isValid() && (completedBlock->isPartitionTable()
                                      || (completedBlock->hasInterface(UDISKS2_BLOCK_INTERFACE)
                                          && completedBlock->interfaceCount() == 1))) {
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
    if (Block *block = device(dbusObjectPath)) {
        if (block && interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE)) {
            block->addInterface(UDISKS2_FILESYSTEM_INTERFACE, interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE));

            // We just received FileSystem interface meaning that this must be mountable.
            // Lower formatting flag from both crypto backing device this self.
            if (block->hasCryptoBackingDevice()) {
                Block *cryptoBackingDevice = device(block->cryptoBackingDeviceObjectPath());
                if (cryptoBackingDevice) {
                    cryptoBackingDevice->setFormatting(false);
                }
            }
            block->setFormatting(false);
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
    if (cryptoBackingDevicePath != QLatin1String("/") && (cryptoBackingDevice = device(cryptoBackingDevicePath))) {
        block->blockSignals(true);
        block->setFormatting(cryptoBackingDevice->isFormatting());
        block->blockSignals(false);
    }
}

void BlockDevices::dumpBlocks() const
{
    if (!m_activeBlockDevices.isEmpty())
        qCInfo(lcMemoryCardLog) << "======== Active block devices:" << m_activeBlockDevices.count();
    else
        qCInfo(lcMemoryCardLog) << "======== No active block devices";

    for (QMap<QString, Block *>::const_iterator i = m_activeBlockDevices.constBegin(); i != m_activeBlockDevices.constEnd(); ++i) {
        i.value()->dumpInfo();
    }

    if (!m_blockDevices.isEmpty())
        qCInfo(lcMemoryCardLog) << "======== Existing block devices:" << m_blockDevices.count();
    else
        qCInfo(lcMemoryCardLog) << "======== No existing block devices";

    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.constBegin(); i != m_blockDevices.constEnd(); ++i) {
        i.value()->dumpInfo();
    }
}

void BlockDevices::complete(Block *block, bool forceAccept)
{
    // Wait queried D-Bus properties getters to finalize for each created block device
    // before exposing them outside.
    // Mark a block as pending if block devices is not yet populated.
    if (!populated()) {
        m_pendingBlockDevices.insert(block->path(), block);
        return;
    }

    if (!hintAuto(block)) {
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

    bool willAccept = !unlocked && (block->isPartition()
                                    || block->isMountable()
                                    || block->isEncrypted()
                                    || block->isFormatting()
                                    || forceAccept);
    qCInfo(lcMemoryCardLog) << "Completed block" << qPrintable(block->path())
                            << "is" << (willAccept ? "accepted" : block->isPartition() ? "kept" : "rejected");
    block->dumpInfo();

    if (willAccept) {
        // Hope that somebody will handle this signal and call insert()
        // to add this block to m_activeBlockDevices.
        m_blockDevices.insert(block->path(), block);
        emit newBlock(block, false);
    } else if (block->isPartition()) {
        // Silently keep partitions around so that we can filter out
        // partition tables in timerEvent().
        m_blockDevices.insert(block->path(), block);
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
        if (e->timerId() == waiter->timer) {
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
        if (m_blockCount <= 0) {
            m_populated = true;

            for (Block *block : m_pendingBlockDevices) {
                complete(block);
            }
            m_pendingBlockDevices.clear();
            emit externalStoragesPopulated();
            m_blockCount = 0;
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
