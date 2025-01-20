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

#include "udisks2monitor_p.h"
#include "udisks2block_p.h"
#include "udisks2blockdevices_p.h"
#include "udisks2job_p.h"
#include "udisks2defines.h"
#include "nemo-dbus/dbus.h"

#include "partitionmanager_p.h"
#include "logging_p.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMetaType>

struct ErrorEntry {
    Partition::Error errorCode;
    const char *dbusErrorName;
};

// These are "copied" error from udiskserror.c so that we do not link against it.
static const ErrorEntry dbus_error_entries[] =
{
    { Partition::ErrorFailed,                 "org.freedesktop.UDisks2.Error.Failed" },
    { Partition::ErrorCancelled,              "org.freedesktop.UDisks2.Error.Cancelled" },
    { Partition::ErrorAlreadyCancelled,       "org.freedesktop.UDisks2.Error.AlreadyCancelled" },
    { Partition::ErrorNotAuthorized,          "org.freedesktop.UDisks2.Error.NotAuthorized" },
    { Partition::ErrorNotAuthorizedCanObtain, "org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain" },
    { Partition::ErrorNotAuthorizedDismissed, "org.freedesktop.UDisks2.Error.NotAuthorizedDismissed" },
    { Partition::ErrorAlreadyMounted,         UDISKS2_ERROR_ALREADY_MOUNTED },
    { Partition::ErrorNotMounted,             "org.freedesktop.UDisks2.Error.NotMounted" },
    { Partition::ErrorOptionNotPermitted,     "org.freedesktop.UDisks2.Error.OptionNotPermitted" },
    { Partition::ErrorMountedByOtherUser,     "org.freedesktop.UDisks2.Error.MountedByOtherUser" },
    { Partition::ErrorAlreadyUnmounting,      UDISKS2_ERROR_ALREADY_UNMOUNTING },
    { Partition::ErrorNotSupported,           "org.freedesktop.UDisks2.Error.NotSupported" },
    { Partition::ErrorTimedout,               "org.freedesktop.UDisks2.Error.Timedout" },
    { Partition::ErrorWouldWakeup,            "org.freedesktop.UDisks2.Error.WouldWakeup" },
    { Partition::ErrorDeviceBusy,             "org.freedesktop.UDisks2.Error.DeviceBusy" }
};

UDisks2::Monitor *UDisks2::Monitor::sharedInstance = nullptr;

UDisks2::Monitor *UDisks2::Monitor::instance()
{
    Q_ASSERT(sharedInstance);

    return sharedInstance;
}

UDisks2::Monitor::Monitor(PartitionManagerPrivate *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_blockDevices(BlockDevices::instance())
{
    Q_ASSERT(!sharedInstance);
    sharedInstance = this;

    qDBusRegisterMetaType<UDisks2::InterfacePropertyMap>();
    QDBusConnection systemBus = QDBusConnection::systemBus();

    connect(systemBus.interface(), &QDBusConnectionInterface::callWithCallbackFailed,
            this, [this](const QDBusError &error, const QDBusMessage &call) {
        qCInfo(lcMemoryCardLog) << "====================================================";
        qCInfo(lcMemoryCardLog) << "DBus call with callback failed:" << error.message();
        qCInfo(lcMemoryCardLog) << "Name:" << error.name();
        qCInfo(lcMemoryCardLog) << "Error name" << call.errorName();
        qCInfo(lcMemoryCardLog) << "Error message:" << call.errorMessage();
        qCInfo(lcMemoryCardLog) << "Call interface:" << call.interface();
        qCInfo(lcMemoryCardLog) << "Call path:" << call.path();
        qCInfo(lcMemoryCardLog) << "====================================================";
        emit errorMessage(call.path(), error.name());
    });

    if (!systemBus.connect(
                UDISKS2_SERVICE,
                UDISKS2_PATH,
                DBUS_OBJECT_MANAGER_INTERFACE,
                interfacesAddedSignal,
                this,
                SLOT(interfacesAdded(QDBusObjectPath, UDisks2::InterfacePropertyMap)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to interfaces added signal:" << qPrintable(systemBus.lastError().message());
    }

    if (!systemBus.connect(
                UDISKS2_SERVICE,
                UDISKS2_PATH,
                DBUS_OBJECT_MANAGER_INTERFACE,
                interfacesRemovedSignal,
                this,
                SLOT(interfacesRemoved(QDBusObjectPath, QStringList)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to interfaces removed signal:" << qPrintable(systemBus.lastError().message());
    }

    if (!QDBusConnection::systemBus().connect(
                UDISKS2_SERVICE,
                QString(),
                UDISKS2_JOB_INTERFACE,
                QStringLiteral("Completed"),
                this,
                SLOT(jobCompleted(bool, QString)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to jobs completed signal:" << qPrintable(systemBus.lastError().message());
    }

    getBlockDevices();

    connect(m_blockDevices, &BlockDevices::newBlock, this, &Monitor::handleNewBlock);
}

UDisks2::Monitor::~Monitor()
{
    sharedInstance = nullptr;
    qDeleteAll(m_jobsToWait);
    m_jobsToWait.clear();

    delete m_blockDevices;
    m_blockDevices = nullptr;
}

// TODO : Move lock, unlock, mount, unmount, format inside udisks2block.cpp
// unlock, mount, format should be considered completed only after file system interface re-appears for the block.
void UDisks2::Monitor::lock(const QString &devicePath)
{
    QVariantList arguments;
    QVariantMap options;
    arguments << options;

    if (Block *block = m_blockDevices->find(devicePath)) {
        block->dumpInfo();
        block->setLocking();

        // Unmount if mounted.
        if (!block->mountPath().isEmpty()) {
            m_operationQueue.enqueue(Operation(UDISKS2_ENCRYPTED_LOCK, devicePath));
            unmount(block->device());
        } else {
            startLuksOperation(devicePath, UDISKS2_ENCRYPTED_LOCK, m_blockDevices->objectPath(devicePath), arguments);
        }
    } else {
        qCWarning(lcMemoryCardLog) << "Block device" << devicePath << "not found";
    }
}

void UDisks2::Monitor::unlock(const QString &devicePath, const QString &passphrase)
{
    QVariantList arguments;
    arguments << passphrase;
    QVariantMap options;
    arguments << options;
    startLuksOperation(devicePath, UDISKS2_ENCRYPTED_UNLOCK, m_blockDevices->objectPath(devicePath), arguments);
}

void UDisks2::Monitor::mount(const QString &devicePath)
{
    QVariantList arguments;
    QVariantMap options;

    if (Block *block = m_blockDevices->find(devicePath)) {
        QString objectPath;
        if (block->device() == devicePath) {
            objectPath = block->path();
        } else if (block->cryptoBackingDevicePath() == devicePath) {
            objectPath = block->cryptoBackingDeviceObjectPath();
        }

        // Find has the same condition.
        Q_ASSERT(!objectPath.isEmpty());

        options.insert(QStringLiteral("fstype"), block->idType());
        arguments << options;
        startMountOperation(devicePath, UDISKS2_FILESYSTEM_MOUNT, objectPath, arguments);
    } else {
        emit mountError(Partition::ErrorOptionNotPermitted);
        emit status(devicePath, Partition::Unmounted);
    }
}

void UDisks2::Monitor::unmount(const QString &devicePath)
{
    QVariantList arguments;
    QVariantMap options;
    arguments << options;
    startMountOperation(devicePath, UDISKS2_FILESYSTEM_UNMOUNT, m_blockDevices->objectPath(devicePath), arguments);
}

void UDisks2::Monitor::format(const QString &devicePath, const QString &filesystemType, const QVariantMap &arguments)
{
    if (devicePath.isEmpty()) {
        qCCritical(lcMemoryCardLog) << "Cannot format without device name";
        return;
    }

    QStringList fsList = m_manager->supportedFileSystems();
    if (!fsList.contains(filesystemType)) {
        qCWarning(lcMemoryCardLog) << "Can only format" << fsList.join(", ") << "filesystems.";
        return;
    }

    const QString objectPath = m_blockDevices->objectPath(devicePath);
    PartitionManagerPrivate::Partitions affectedPartitions;
    lookupPartitions(affectedPartitions, QStringList() << objectPath);

    for (auto partition : affectedPartitions) {
        // Mark block to formatting state.
        if (Block *block = m_blockDevices->find(devicePath)) {
            block->setFormatting(true);
        }

        // Lock unlocked block device before formatting.
        if (!partition->cryptoBackingDevicePath.isEmpty()) {
            lock(partition->cryptoBackingDevicePath);
            m_operationQueue.enqueue(Operation(UDISKS2_BLOCK_FORMAT, partition->cryptoBackingDevicePath,
                                               objectPath, filesystemType, arguments));
            return;
        } else if (partition->status == Partition::Mounted) {
            m_operationQueue.enqueue(Operation(UDISKS2_BLOCK_FORMAT, devicePath, objectPath, filesystemType, arguments));
            unmount(devicePath);
            return;
        }
    }

    doFormat(devicePath, objectPath, filesystemType, arguments);
}

void UDisks2::Monitor::interfacesAdded(const QDBusObjectPath &objectPath, const UDisks2::InterfacePropertyMap &interfaces)
{
    QString path = objectPath.path();
    qCDebug(lcMemoryCardLog) << "UDisks interface added:" << path;
    qCInfo(lcMemoryCardLog) << "UDisks dump interface:" << interfaces;
    // A device must have file system or partition so that it can added to the model.
    // Devices without partition table can have a filesystem interface.
    if (path.startsWith(QStringLiteral("/org/freedesktop/UDisks2/block_devices/"))) {
        m_blockDevices->createBlockDevice(path, interfaces);
    } else if (path.startsWith(QStringLiteral("/org/freedesktop/UDisks2/jobs"))) {
        QVariantMap dict = interfaces.value(UDISKS2_JOB_INTERFACE);
        QString operation = dict.value(UDISKS2_JOB_KEY_OPERATION, QString()).toString();

        if (operation == UDISKS2_JOB_OP_ENC_LOCK
                || operation == UDISKS2_JOB_OP_ENC_UNLOCK
                || operation == UDISKS2_JOB_OP_FS_MOUNT
                || operation == UDISKS2_JOB_OP_FS_UNMOUNT
                || operation == UDISKS2_JOB_OP_CLEANUP
                || operation == UDISKS2_JOB_OF_FS_FORMAT) {
            UDisks2::Job *job = new UDisks2::Job(path, dict);
            updatePartitionStatus(job, true);

            connect(job, &UDisks2::Job::completed, this, [this](bool success) {
                UDisks2::Job *job = qobject_cast<UDisks2::Job *>(sender());
                job->dumpInfo();
                if (job->operation() != Job::Lock) {
                    updatePartitionStatus(job, success);
                } else {
                    for (const QString &dbusObjectPath : job->objects()) {
                        m_blockDevices->lock(dbusObjectPath);
                    }
                }
            });

            if (job->operation() == Job::Format) {
                for (const QString &objectPath : job->objects()) {
                    if (UDisks2::Block *block = m_blockDevices->device(objectPath)) {
                        block->blockSignals(true);
                        block->setFormatting(true);
                        block->blockSignals(false);
                    }
                }
            }

            m_jobsToWait.insert(path, job);
            job->dumpInfo();
        }
    }
}

void UDisks2::Monitor::interfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces)
{
    QString path = objectPath.path();
    qCDebug(lcMemoryCardLog) << "UDisks interface removed:" << path;
    qCInfo(lcMemoryCardLog) << "UDisks dump interface:" << interfaces;

    if (m_jobsToWait.contains(path)) {
        UDisks2::Job *job = m_jobsToWait.take(path);
        // Make sure job is completed. Not sure if we can assume it success really.
        if (!job->isCompleted()) {
            qWarning() << "Udisks2 job removed without finishing. Assuming completed" << path;
            job->complete(true);
        }
        delete job;
    } else if (m_blockDevices->contains(path) && interfaces.contains(UDISKS2_BLOCK_INTERFACE)) {
        // Cleanup partitions first.
        PartitionManagerPrivate::Partitions removedPartitions;
        QStringList blockDevPaths = { path };
        lookupPartitions(removedPartitions, blockDevPaths);
        m_manager->remove(removedPartitions);

        m_blockDevices->remove(path);
    } else {
        m_blockDevices->removeInterfaces(path, interfaces);
    }
}

void UDisks2::Monitor::setPartitionProperties(QExplicitlySharedDataPointer<PartitionPrivate> &partition,
                                              const UDisks2::Block *blockDevice)
{
    QString label = blockDevice->idLabel();
    if (label.isEmpty()) {
        label = blockDevice->idUUID();
    }

    qCDebug(lcMemoryCardLog) << "Set partition properties";
    blockDevice->dumpInfo();

    partition->devicePath = blockDevice->device();
    QString deviceName = partition->devicePath.section(QChar('/'), 2);
    partition->deviceName = deviceName;
    partition->deviceRoot = deviceRoot.match(deviceName).hasMatch();

    partition->mountPath = blockDevice->mountPath();
    partition->deviceLabel = label;
    partition->filesystemType = blockDevice->idType();
    partition->isSupportedFileSystemType = m_manager->supportedFileSystems().contains(partition->filesystemType);
    partition->readOnly = blockDevice->isReadOnly();
    partition->canMount = blockDevice->isMountable() && m_manager->supportedFileSystems().contains(partition->filesystemType);

    if (blockDevice->isFormatting()) {
        partition->status = Partition::Formatting;
    } else if (blockDevice->isEncrypted()) {
        partition->status = Partition::Locked;
    } else if (blockDevice->mountPath().isEmpty()) {
        partition->status = Partition::Unmounted;
    } else {
        partition->status = Partition::Mounted;
    }
    partition->isCryptoDevice = blockDevice->isCryptoBlock();
    partition->isEncrypted = blockDevice->isEncrypted();
    partition->cryptoBackingDevicePath = blockDevice->cryptoBackingDevicePath();

    QVariantMap drive;

    QString connectionBus = blockDevice->connectionBus();
    if (connectionBus == QLatin1String("sdio")) {
        drive.insert(QLatin1String("connectionBus"), Partition::SDIO);
    } else if (connectionBus == QLatin1String("usb")) {
        drive.insert(QLatin1String("connectionBus"), Partition::USB);
    } else if (connectionBus == QLatin1String("ieee1394")) {
        drive.insert(QLatin1String("connectionBus"), Partition::IEEE1394);
    } else {
        drive.insert(QLatin1String("connectionBus"), Partition::UnknownBus);
    }
    drive.insert(QLatin1String("model"), blockDevice->driveModel());
    drive.insert(QLatin1String("vendor"), blockDevice->driveVendor());
    partition->drive = drive;
}

void UDisks2::Monitor::updatePartitionProperties(const UDisks2::Block *blockDevice)
{
    bool hasCryptoBackingDevice = blockDevice->hasCryptoBackingDevice();
    const QString cryptoBackingDevicePath = blockDevice->cryptoBackingDevicePath();

    for (auto partition : m_manager->m_partitions) {
        if ((partition->devicePath == blockDevice->device())
                || (hasCryptoBackingDevice && (partition->devicePath == cryptoBackingDevicePath))) {
            setPartitionProperties(partition, blockDevice);
            partition->valid = true;
            m_manager->refresh(partition.data());
        }
    }
}

void UDisks2::Monitor::updatePartitionStatus(const UDisks2::Job *job, bool success)
{
    UDisks2::Job::Operation operation = job->operation();
    PartitionManagerPrivate::Partitions affectedPartitions;
    lookupPartitions(affectedPartitions, job->objects());
    if (operation == UDisks2::Job::Lock || operation == UDisks2::Job::Unlock) {
        for (auto partition : affectedPartitions) {
            Partition::Status oldStatus = partition->status;
            if (success) {
                if (job->status() == UDisks2::Job::Added) {
                    partition->activeState = QStringLiteral("inactive");
                    partition->status = operation == UDisks2::Job::Unlock ? Partition::Unlocking : Partition::Locking;
                } else {
                    partition->activeState = QStringLiteral("inactive");
                    partition->status = operation == UDisks2::Job::Unlock ? Partition::Unmounted : Partition::Locked;
                }
            } else {
                partition->activeState = QStringLiteral("failed");
                partition->status = operation == UDisks2::Job::Unlock ? Partition::Locked : Partition::Unmounted;
            }
            partition->valid = true;
            if (oldStatus != partition->status) {
                m_manager->refresh(partition.data());
            }
        }
    } else if (operation == UDisks2::Job::Mount || operation == UDisks2::Job::Unmount) {
        for (auto partition : affectedPartitions) {
            Partition::Status oldStatus = partition->status;

            if (success) {
                if (job->status() == UDisks2::Job::Added) {
                    partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("activating")
                                                                              : QStringLiteral("deactivating");
                    partition->status = operation == UDisks2::Job::Mount ? Partition::Mounting : Partition::Unmounting;
                } else {
                    // Completed busy unmount job shall stay in mounted state.
                    if (job->deviceBusy() && operation == UDisks2::Job::Unmount)
                        operation = UDisks2::Job::Mount;

                    partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("active")
                                                                              : QStringLiteral("inactive");
                    partition->status = operation == UDisks2::Job::Mount ? Partition::Mounted : Partition::Unmounted;
                }
            } else {
                partition->activeState = QStringLiteral("failed");
                partition->status = operation == UDisks2::Job::Mount ? Partition::Unmounted : Partition::Mounted;
            }

            partition->valid = true;
            partition->mountFailed = job->deviceBusy() ? false : !success;
            if (oldStatus != partition->status) {
                m_manager->refresh(partition.data());
            }
        }
    } else if (operation == UDisks2::Job::Format) {
        for (auto partition : affectedPartitions) {
            Partition::Status oldStatus = partition->status;
            if (success) {
                if (job->status() == UDisks2::Job::Added) {
                    partition->activeState = QStringLiteral("inactive");
                    partition->status = Partition::Formatting;
                    partition->bytesAvailable = 0;
                    partition->bytesTotal = 0;
                    partition->bytesFree = 0;
                    partition->filesystemType.clear();
                    partition->canMount = false;
                    partition->valid = false;
                }
            } else {
                partition->activeState = QStringLiteral("failed");
                partition->status = Partition::Unmounted;
                partition->valid = false;
            }

            if (oldStatus != partition->status) {
                m_manager->refresh(partition.data());
            }
        }
    }
}

void UDisks2::Monitor::startLuksOperation(const QString &devicePath, const QString &dbusMethod,
                                          const QString &dbusObjectPath, const QVariantList &arguments)
{
    Q_ASSERT(dbusMethod == UDISKS2_ENCRYPTED_LOCK || dbusMethod == UDISKS2_ENCRYPTED_UNLOCK);

    if (devicePath.isEmpty()) {
        qCCritical(lcMemoryCardLog) << "Cannot" << dbusMethod.toLower() << "without device name";
        return;
    }

    QDBusInterface udisks2Interface(UDISKS2_SERVICE,
                                    dbusObjectPath,
                                    UDISKS2_ENCRYPTED_INTERFACE,
                                    QDBusConnection::systemBus());

    QDBusPendingCall pendingCall = udisks2Interface.asyncCallWithArgumentList(dbusMethod, arguments);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, [this, devicePath, dbusMethod](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
                emit status(devicePath, Partition::Locked);
            } else {
                emit status(devicePath, Partition::Unmounted);
            }
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            QByteArray errorData = error.name().toLocal8Bit();
            const char *errorCStr = errorData.constData();

            qCWarning(lcMemoryCardLog) << dbusMethod << "error:" << errorCStr << error.message();

            for (uint i = 0; i < sizeof(dbus_error_entries) / sizeof(ErrorEntry); i++) {
                if (strcmp(dbus_error_entries[i].dbusErrorName, errorCStr) == 0) {
                    if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
                        emit lockError(dbus_error_entries[i].errorCode);
                        break;
                    } else {
                        emit unlockError(dbus_error_entries[i].errorCode);
                        break;
                    }
                }
            }

            if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
                // All other errors will revert back the previous state.
                emit status(devicePath, Partition::Unmounted); // TODO: could it have been in Mounted state?
            } else if (dbusMethod == UDISKS2_ENCRYPTED_UNLOCK) {
                // All other errors will revert back the previous state.
                emit status(devicePath, Partition::Locked);
            }
        }

        watcher->deleteLater();
    });

    if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
        emit status(devicePath, Partition::Locking);
    } else {
        emit status(devicePath, Partition::Unlocking);
    }
}

void UDisks2::Monitor::startMountOperation(const QString &devicePath, const QString &dbusMethod,
                                           const QString &dbusObjectPath, const QVariantList &arguments)
{
    Q_ASSERT(dbusMethod == UDISKS2_FILESYSTEM_MOUNT || dbusMethod == UDISKS2_FILESYSTEM_UNMOUNT);

    if (devicePath.isEmpty()) {
        qCCritical(lcMemoryCardLog) << "Cannot" << dbusMethod.toLower() << "without device name";
        return;
    }

    QDBusInterface udisks2Interface(UDISKS2_SERVICE,
                                    dbusObjectPath,
                                    UDISKS2_FILESYSTEM_INTERFACE,
                                    QDBusConnection::systemBus());

    QDBusPendingCall pendingCall = udisks2Interface.asyncCallWithArgumentList(dbusMethod, arguments);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, [this, devicePath, dbusMethod](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            Block *block = m_blockDevices->find(devicePath);
            if (block && block->isFormatting()) {
                // Do nothing
            } else if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
                emit status(devicePath, Partition::Mounted);
            } else {
                emit status(devicePath, Partition::Unmounted);
            }
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            QByteArray errorData = error.name().toLocal8Bit();
            const char *errorCStr = errorData.constData();

            qCWarning(lcMemoryCardLog) << "udisks2 error: " << dbusMethod << "error:" << errorCStr;

            for (uint i = 0; i < sizeof(dbus_error_entries) / sizeof(ErrorEntry); i++) {
                if (strcmp(dbus_error_entries[i].dbusErrorName, errorCStr) == 0) {
                    if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
                        emit mountError(dbus_error_entries[i].errorCode);
                        break;
                    } else {
                        emit unmountError(dbus_error_entries[i].errorCode);
                        break;
                    }
                }
            }

            if (strcmp(UDISKS2_ERROR_ALREADY_UNMOUNTING, errorCStr) == 0) {
                // Do nothing
            } else if (strcmp(UDISKS2_ERROR_ALREADY_MOUNTED, errorCStr) == 0) {
                emit status(devicePath, Partition::Mounted);
            } else if (dbusMethod == UDISKS2_FILESYSTEM_UNMOUNT) {
                // All other errors will revert back the previous state.
                emit status(devicePath, Partition::Mounted);
            } else if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
                // All other errors will revert back the previous state.
                emit status(devicePath, Partition::Unmounted);
            }
        }

        watcher->deleteLater();
    });

    Block *block = m_blockDevices->find(devicePath);
    if (block && block->isFormatting()) {
        emit status(devicePath, Partition::Formatting);
    } else if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
        emit status(devicePath, Partition::Mounting);
    } else {
        emit status(devicePath, Partition::Unmounting);
    }
}

void UDisks2::Monitor::lookupPartitions(PartitionManagerPrivate::Partitions &affectedPartitions,
                                        const QStringList &objects)
{
    QStringList blockDevs = m_blockDevices->devicePaths(objects);
    for (const QString &dev : blockDevs) {
        for (auto partition : m_manager->m_partitions) {
            if (partition->devicePath == dev) {
                affectedPartitions << partition;
            }
        }
    }
}

void UDisks2::Monitor::createPartition(const UDisks2::Block *block)
{
    QExplicitlySharedDataPointer<PartitionPrivate> partition(new PartitionPrivate(m_manager.data()));
    partition->storageType = Partition::External;
    partition->devicePath = block->device();
    partition->bytesTotal = block->size();
    setPartitionProperties(partition, block);
    partition->valid = true;
    m_manager->add(partition);
}

void UDisks2::Monitor::doFormat(const QString &devicePath, const QString &dbusObjectPath,
                                const QString &filesystemType, const QVariantMap &arguments)
{
    QDBusInterface blockDeviceInterface(UDISKS2_SERVICE,
                                    dbusObjectPath,
                                    UDISKS2_BLOCK_INTERFACE,
                                    QDBusConnection::systemBus());

    QDBusPendingCall pendingCall = blockDeviceInterface.asyncCall(UDISKS2_BLOCK_FORMAT, filesystemType, arguments);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, [this, devicePath, dbusObjectPath, arguments](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            emit status(devicePath, Partition::Formatted);
        } else if (watcher->isError()) {
            Block *block = m_blockDevices->find(devicePath);
            if (block) {
                block->setFormatting(false);
            }
            QDBusError error = watcher->error();
            QByteArray errorData = error.name().toLocal8Bit();
            const char *errorCStr = errorData.constData();
            qCWarning(lcMemoryCardLog) << "Format error:" << errorCStr << dbusObjectPath;

            for (uint i = 0; i < sizeof(dbus_error_entries) / sizeof(ErrorEntry); i++) {
                if (strcmp(dbus_error_entries[i].dbusErrorName, errorCStr) == 0) {
                    emit formatError(dbus_error_entries[i].errorCode);
                    break;
                }
            }
        }
        watcher->deleteLater();
    });
}

void UDisks2::Monitor::getBlockDevices()
{
    QDBusInterface managerInterface(UDISKS2_SERVICE,
                                    UDISKS2_MANAGER_PATH,
                                    UDISKS2_MANAGER_INTERFACE,
                                    QDBusConnection::systemBus());
    QDBusPendingCall pendingCall = managerInterface.asyncCallWithArgumentList(
                QStringLiteral("GetBlockDevices"),
                QVariantList() << QVariantMap());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            QDBusPendingReply<QList<QDBusObjectPath> > reply = *watcher;
            const QList<QDBusObjectPath> blockDevicePaths = reply.argumentAt<0>();
            m_blockDevices->createBlockDevices(blockDevicePaths);
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            qCWarning(lcMemoryCardLog) << "Unable to enumerate block devices:" << error.name() << error.message();
        }
    });
}

void UDisks2::Monitor::connectSignals(UDisks2::Block *block)
{
    connect(block, &UDisks2::Block::formatted, this, [this]() {
        UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
        if (m_blockDevices->contains(block->path())) {
            for (auto partition : m_manager->m_partitions) {
                if (partition->devicePath == block->device()) {
                    partition->status = Partition::Formatted;
                    partition->activeState = QStringLiteral("inactive");
                    partition->valid = true;
                    m_manager->refresh(partition.data());
                }
            }
        }
    }, Qt::UniqueConnection);

    // When block info updated
    connect(block, &UDisks2::Block::updated, this, [this]() {
        UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
        if (m_blockDevices->contains(block->path())) {
            updatePartitionProperties(block);
        }
    }, Qt::UniqueConnection);

    connect(block, &UDisks2::Block::mountPathChanged, this, [this]() {
        UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
        // Both updatePartitionStatus and updatePartitionProperties
        // emits partition refresh => latter one is enough.

        m_manager->blockSignals(true);
        QVariantMap data;
        data.insert(UDISKS2_JOB_KEY_OPERATION, block->mountPath().isEmpty() ? UDISKS2_JOB_OP_FS_UNMOUNT
                                                                            : UDISKS2_JOB_OP_FS_MOUNT);
        data.insert(UDISKS2_JOB_KEY_OBJECTS, QStringList() << block->path());
        qCDebug(lcMemoryCardLog) << "New partition status:" << data;
        UDisks2::Job tmpJob(QString(), data);
        tmpJob.complete(true);
        updatePartitionStatus(&tmpJob, true);
        m_manager->blockSignals(false);

        updatePartitionProperties(block);

        if (!m_operationQueue.isEmpty()) {
            Operation op = m_operationQueue.head();
            if (op.command == UDISKS2_BLOCK_FORMAT && block->mountPath().isEmpty()) {
                m_operationQueue.dequeue();
                doFormat(op.devicePath, op.dbusObjectPath, op.filesystemType, op.arguments);
            } else if (op.command == UDISKS2_ENCRYPTED_LOCK && block->mountPath().isEmpty()) {
                m_operationQueue.dequeue();
                lock(op.devicePath);
            }
        }
    }, Qt::UniqueConnection);

    connect(block, &UDisks2::Block::blockRemoved, this, [this](const QString &device) {
        PartitionManagerPrivate::Partitions removedPartitions;
        for (auto partition : m_manager->m_partitions) {
            if (partition->devicePath == device) {
                removedPartitions << partition;
            }
        }
        m_manager->remove(removedPartitions);
    });
}

void UDisks2::Monitor::handleNewBlock(UDisks2::Block *block, bool forceCreatePartition)
{
    const QString cryptoBackingDeviceObjectPath = block->cryptoBackingDeviceObjectPath();
    if (block->hasCryptoBackingDevice() && m_blockDevices->contains(cryptoBackingDeviceObjectPath)) {
        // Deactivate crypto backing device.
        m_blockDevices->deactivate(cryptoBackingDeviceObjectPath);
        updatePartitionProperties(block);
    } else if (!m_blockDevices->contains(block->path()) || forceCreatePartition) {
        m_blockDevices->insert(block->path(), block);
        createPartition(block);

        if (block->isFormatting()) {
            if (!m_operationQueue.isEmpty()) {
                Operation op = m_operationQueue.head();
                if (op.command == UDISKS2_BLOCK_FORMAT) {
                    m_operationQueue.dequeue();
                    QMetaObject::invokeMethod(this, "doFormat", Qt::QueuedConnection,
                                              Q_ARG(QString, op.devicePath), Q_ARG(QString, op.dbusObjectPath),
                                              Q_ARG(QString, op.filesystemType), Q_ARG(QVariantMap, op.arguments));
                }
            } else {
                qCDebug(lcMemoryCardLog) << "Formatting cannot be executed. Is block mounted:" << !block->mountPath().isEmpty();
            }
        }
    }

    connectSignals(block);
}

void UDisks2::Monitor::jobCompleted(bool success, const QString &msg)
{
    QString jobPath = message().path();
    if (m_jobsToWait.contains(jobPath)) {
        m_jobsToWait[jobPath]->complete(success, msg);
    }
}
