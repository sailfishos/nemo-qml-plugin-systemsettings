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
    Q_ASSERT(!sharedInstance);

    return sharedInstance;
}

UDisks2::Monitor::Monitor(PartitionManagerPrivate *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    Q_ASSERT(!sharedInstance);
    sharedInstance = this;

    qDBusRegisterMetaType<InterfaceAndPropertyMap>();
    QDBusConnection systemBus = QDBusConnection::systemBus();

    connect(systemBus.interface(), &QDBusConnectionInterface::callWithCallbackFailed, this, [this](const QDBusError &error, const QDBusMessage &call) {
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
                QStringLiteral("InterfacesAdded"),
                this,
                SLOT(interfacesAdded(QDBusObjectPath, InterfaceAndPropertyMap)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to interfaces added signal:" << qPrintable(systemBus.lastError().message());
    }

    if (!systemBus.connect(
                UDISKS2_SERVICE,
                UDISKS2_PATH,
                DBUS_OBJECT_MANAGER_INTERFACE,
                QStringLiteral("InterfacesRemoved"),
                this,
                SLOT(interfacesRemoved(QDBusObjectPath, QStringList)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to interfaces removed signal:" << qPrintable(systemBus.lastError().message());
    }

    getBlockDevices();
}

UDisks2::Monitor::~Monitor()
{
    sharedInstance = nullptr;
    qDeleteAll(m_jobsToWait);
    m_jobsToWait.clear();
}

// TODO : Move lock, unlock, mount, unmount, format inside udisks2block.cpp
// unlock, mount, format should be considered completed only after file system interface re-appears for the block.
void UDisks2::Monitor::lock(const QString &deviceName)
{
    QVariantList arguments;
    QVariantMap options;
    arguments << options;
    startLuksOperation(deviceName, UDISKS2_ENCRYPTED_LOCK, objectPath(deviceName), arguments);
}

void UDisks2::Monitor::unlock(const QString &deviceName, const QString &passphrase)
{
    QVariantList arguments;
    arguments << passphrase;
    QVariantMap options;
    arguments << options;
    startLuksOperation(deviceName, UDISKS2_ENCRYPTED_UNLOCK, objectPath(deviceName), arguments);
}

void UDisks2::Monitor::mount(const QString &deviceName)
{
    QVariantList arguments;
    QVariantMap options;
    options.insert(QStringLiteral("fstype"), QString());
    arguments << options;
    startMountOperation(deviceName, UDISKS2_FILESYSTEM_MOUNT, objectPath(deviceName), arguments);
}

void UDisks2::Monitor::unmount(const QString &deviceName)
{
    QVariantList arguments;
    QVariantMap options;
    arguments << options;
    startMountOperation(deviceName, UDISKS2_FILESYSTEM_UNMOUNT, objectPath(deviceName), arguments);
}

void UDisks2::Monitor::format(const QString &deviceName, const QString &type, const QString &label, const QString &passphrase)
{
    if (deviceName.isEmpty()) {
        qCCritical(lcMemoryCardLog) << "Cannot format without device name";
        return;
    }

    QStringList fsList = m_manager->supportedFileSystems();
    if (!fsList.contains(type)) {
        qCWarning(lcMemoryCardLog) << "Can only format" << fsList.join(", ") << "filesystems.";
        return;
    }

    QVariantHash arguments;
    arguments.insert(QStringLiteral("label"), QString(label));
    arguments.insert(QStringLiteral("no-block"), true);
    arguments.insert(QStringLiteral("update-partition-type"), true);
    if (!passphrase.isEmpty()) {
        arguments.insert(QStringLiteral("encrypt.passphrase"), passphrase);
    }

    const QString objectPath = this->objectPath(deviceName);
    PartitionManagerPrivate::Partitions affectedPartitions;
    lookupPartitions(affectedPartitions, QStringList() << objectPath);

    for (auto partition : affectedPartitions) {
        if (partition->status == Partition::Mounted) {
            m_operationQueue.enqueue(Operation(QStringLiteral("format"), deviceName, objectPath, type, arguments));
            unmount(deviceName);
            return;
        }
    }

    doFormat(deviceName, objectPath, type, arguments);
}

void UDisks2::Monitor::interfacesAdded(const QDBusObjectPath &objectPath, const InterfaceAndPropertyMap &interfaces)
{
    QString path = objectPath.path();
    qCInfo(lcMemoryCardLog) << "UDisks interface added:" << path << interfaces << externalBlockDevice(path);
    // External device must have file system or partition so that it can added to the model.
    // Devices without partition table have filesystem interface.

    if ((interfaces.contains(UDISKS2_FILESYSTEM_INTERFACE) ||
         interfaces.contains(UDISKS2_ENCRYPTED_INTERFACE)) && externalBlockDevice(path)) {
        bool hasFileSystem = interfaces.contains(UDISKS2_FILESYSTEM_INTERFACE);
        if (m_blockDevices.contains(path)) {
            UDisks2::Block *block = m_blockDevices.value(path);
            if (hasFileSystem) {
                block->setMountable(true);
            } else {
                block->setEncrypted(true);
            }
        } else {
            QVariantMap dict = interfaces.value(UDISKS2_BLOCK_INTERFACE);
            createBlockDevice(path, interfaces);
        }
    } else if (path.startsWith(QStringLiteral("/org/freedesktop/UDisks2/jobs"))) {
        QVariantMap dict = interfaces.value(UDISKS2_JOB_INTERFACE);
        QString operation = dict.value(UDISKS2_JOB_KEY_OPERATION, QString()).toString();
        if (operation == UDISKS2_JOB_OP_ENC_LOCK ||
                operation == UDISKS2_JOB_OP_ENC_UNLOCK ||
                operation == UDISKS2_JOB_OP_FS_MOUNT ||
                operation == UDISKS2_JOB_OP_FS_UNMOUNT ||
                operation == UDISKS2_JOB_OP_CLEANUP ||
                operation == UDISKS2_JOB_OF_FS_FORMAT) {
            UDisks2::Job *job = new UDisks2::Job(path, dict);
            updatePartitionStatus(job, true);

            connect(job, &UDisks2::Job::completed, this, [this](bool success) {
                UDisks2::Job *job = qobject_cast<UDisks2::Job *>(sender());
                updatePartitionStatus(job, success);
            });
            m_jobsToWait.insert(path, job);
        }
    }
}

void UDisks2::Monitor::interfacesRemoved(const QDBusObjectPath &objectPath, const QStringList &interfaces)
{
    QString path = objectPath.path();
    qCInfo(lcMemoryCardLog) << "UDisks interface removed:" << path << interfaces;

    if (m_jobsToWait.contains(path)) {
        UDisks2::Job *job = m_jobsToWait.take(path);
        job->deleteLater();
    } else if (m_blockDevices.contains(path) && interfaces.contains(UDISKS2_BLOCK_INTERFACE)) {
        // Cleanup partitions first.
        if (externalBlockDevice(path)) {
            PartitionManagerPrivate::Partitions removedPartitions;
            QStringList blockDevPaths = { path };
            lookupPartitions(removedPartitions, blockDevPaths);
            m_manager->remove(removedPartitions);
        }

        UDisks2::Block *block = m_blockDevices.take(path);
        block->deleteLater();
    } else if (m_blockDevices.contains(path) && interfaces.contains(UDISKS2_FILESYSTEM_INTERFACE)) {
        UDisks2::Block *block = m_blockDevices.value(path);
        block->setMountable(false);
    }
}

void UDisks2::Monitor::setPartitionProperties(QExplicitlySharedDataPointer<PartitionPrivate> &partition, const UDisks2::Block *blockDevice)
{
    QString label = blockDevice->idLabel();
    if (label.isEmpty()) {
        label = blockDevice->idUUID();
    }

    qCInfo(lcMemoryCardLog) << "Set partition properties";
    blockDevice->dumpInfo();

    partition->devicePath = blockDevice->device();
    QString deviceName = partition->devicePath.section(QChar('/'), 2);
    partition->deviceName = deviceName;
    partition->deviceRoot = deviceRoot.match(deviceName).hasMatch();

    partition->mountPath = blockDevice->mountPath();
    partition->deviceLabel = label;
    partition->filesystemType = blockDevice->idType();
    partition->readOnly = blockDevice->isReadOnly();
    partition->canMount = blockDevice->isMountable() && m_manager->supportedFileSystems().contains(partition->filesystemType);
    partition->status = blockDevice->isEncrypted() ? Partition::Locked
                                                   : blockDevice->mountPath().isEmpty() ? Partition::Unmounted : Partition::Mounted;
}

void UDisks2::Monitor::updatePartitionProperties(const UDisks2::Block *blockDevice)
{
    bool hasCryptoBackingDevice = blockDevice->hasCryptoBackingDevice();
    const QString cryptoBackingDevice = blockDevice->cryptoBackingDeviceName();
    for (auto partition : m_manager->m_partitions) {
        if ((partition->devicePath == blockDevice->device()) || (hasCryptoBackingDevice && (partition->devicePath == cryptoBackingDevice))) {
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
    lookupPartitions(affectedPartitions, job->value(UDISKS2_JOB_KEY_OBJECTS).toStringList());
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
                    partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("activating") : QStringLiteral("deactivating");
                    partition->status = operation == UDisks2::Job::Mount ? Partition::Mounting : Partition::Unmounting;
                } else {
                    // Completed busy unmount job shall stay in mounted state.
                    if (job->deviceBusy() && operation == UDisks2::Job::Unmount)
                        operation = UDisks2::Job::Mount;

                    partition->activeState = operation == UDisks2::Job::Mount ? QStringLiteral("active") : QStringLiteral("inactive");
                    partition->status = operation == UDisks2::Job::Mount ? Partition::Mounted : Partition::Unmounted;
                }
            } else {
                partition->activeState = QStringLiteral("failed");
                partition->status = operation == UDisks2::Job::Mount ? Partition::Mounted : Partition::Unmounted;
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

                    QString blockDevicePath = UDISKS2_BLOCK_DEVICE_PATH.arg(partition->deviceName);
                    if (m_blockDevices.contains(blockDevicePath)) {
                        Block *block = m_blockDevices.value(blockDevicePath);
                        block->setFormatting();
                    }
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

// Used in UDisks2 InterfacesAdded / InterfacesRemoved signals.
bool UDisks2::Monitor::externalBlockDevice(const QString &deviceName) const
{
    static const QRegularExpression externalBlockDevice(QStringLiteral("^/org/freedesktop/UDisks2/block_devices/%1$").arg(externalDevice));
    return externalBlockDevice.match(deviceName).hasMatch();
}

void UDisks2::Monitor::startLuksOperation(const QString &deviceName, const QString &dbusMethod, const QString &dbusObjectPath, const QVariantList &arguments)
{
    Q_ASSERT(dbusMethod == UDISKS2_ENCRYPTED_LOCK || dbusMethod == UDISKS2_ENCRYPTED_UNLOCK);

    if (deviceName.isEmpty()) {
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
            this, [this, deviceName, dbusMethod](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
                emit status(deviceName, Partition::Locked);
            } else {
                emit status(deviceName, Partition::Unmounted);
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
                emit status(deviceName, Partition::Unmounted); // TODO: could it have been in Mounted state?
            } else if (dbusMethod == UDISKS2_ENCRYPTED_UNLOCK) {
                // All other errors will revert back the previous state.
                emit status(deviceName, Partition::Locked);
            }
        }

        watcher->deleteLater();
    });

    if (dbusMethod == UDISKS2_ENCRYPTED_LOCK) {
        emit status(deviceName, Partition::Locking);
    } else {
        emit status(deviceName, Partition::Unlocking);
    }
}

void UDisks2::Monitor::startMountOperation(const QString &deviceName, const QString &dbusMethod, const QString &dbusObjectPath, const QVariantList &arguments)
{
    Q_ASSERT(dbusMethod == UDISKS2_FILESYSTEM_MOUNT || dbusMethod == UDISKS2_FILESYSTEM_UNMOUNT);

    if (deviceName.isEmpty()) {
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
            this, [this, deviceName, dbusMethod](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
                emit status(deviceName, Partition::Mounted);
            } else {
                emit status(deviceName, Partition::Unmounted);
            }
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            QByteArray errorData = error.name().toLocal8Bit();
            const char *errorCStr = errorData.constData();

            qCWarning(lcMemoryCardLog) << dbusMethod << "error:" << errorCStr;

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
                emit status(deviceName, Partition::Mounted);
            } else if (dbusMethod == UDISKS2_FILESYSTEM_UNMOUNT) {
                // All other errors will revert back the previous state.
                emit status(deviceName, Partition::Mounted);
            } else if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
                // All other errors will revert back the previous state.
                emit status(deviceName, Partition::Unmounted);
            }
        }

        watcher->deleteLater();
    });

    if (dbusMethod == UDISKS2_FILESYSTEM_MOUNT) {
        emit status(deviceName, Partition::Mounting);
    } else {
        emit status(deviceName, Partition::Unmounting);
    }
}

void UDisks2::Monitor::lookupPartitions(PartitionManagerPrivate::Partitions &affectedPartitions, const QStringList &objects)
{
    QStringList blockDevs;
    for (const QString objectPath : objects) {
        for (QMap<QString, Block *>::const_iterator i = m_blockDevices.begin(); i != m_blockDevices.end(); ++i) {
            Block *block = i.value();
            if (block->path() == objectPath) {
                blockDevs << block->device();
            }
        }
    }

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
    PartitionManagerPrivate::Partitions addedPartitions = { partition };
    m_manager->add(addedPartitions);
}

void UDisks2::Monitor::createBlockDevice(const QString &path, const InterfaceAndPropertyMap &interfacePropertyMap)
{
    if (m_blockDevices.contains(path)) {
        return;
    }

    // First guards that we don't create extensively block devices that cannot be
    // external block devices.
    if (externalBlockDevice(path)) {
        UDisks2::Block *block = new UDisks2::Block(path, interfacePropertyMap);

        // Upon creation.
        connect(block, &UDisks2::Block::completed, this, [this]() {
            UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
            if (block->isExternal() && (block->isMountable() || block->isEncrypted())) {
                const QString cryptoBackingDeviceObjectPath = block->cryptoBackingDeviceObjectPath();
                if (block->hasCryptoBackingDevice() && m_blockDevices.contains(cryptoBackingDeviceObjectPath)) {
                    // Update crypto backing device to file system device.
                    UDisks2::Block *cryptoBackingDev = m_blockDevices.value(cryptoBackingDeviceObjectPath);
                    *cryptoBackingDev = *block;

                    m_blockDevices.remove(cryptoBackingDeviceObjectPath);
                    m_blockDevices.insert(cryptoBackingDev->path(), cryptoBackingDev);

                    updatePartitionProperties(cryptoBackingDev);

                    block->deleteLater();
                } else if (!m_blockDevices.contains(block->path())) {
                    m_blockDevices.insert(block->path(), block);
                    createPartition(block);
                }
            } else {
                // This is garbage block device that should not be exposed
                // from the partition model.
                block->deleteLater();
            }
        });

        connect(block, &UDisks2::Block::formatted, this, [this]() {
            UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
            QString blockPath = block->path();
            if (m_blockDevices.contains(blockPath)) {
                for (auto partition : m_manager->m_partitions) {
                    if (partition->devicePath == block->device()) {
                        partition->status = Partition::Formatted;
                        partition->activeState = QStringLiteral("inactive");
                        partition->valid = true;
                        m_manager->refresh(partition.data());
                    }
                }
            }
        });

        // When block info updated
        connect(block, &UDisks2::Block::updated, this, [this]() {
            UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
            QString blockPath = block->path();
            if (m_blockDevices.contains(blockPath)) {
                updatePartitionProperties(block);
            }
        });

        connect(block, &UDisks2::Block::mountPathChanged, this, [this]() {
            UDisks2::Block *block = qobject_cast<UDisks2::Block *>(sender());
            // Both updatePartitionStatus and updatePartitionProperties
            // emits partition refresh => latter one is enough.

            m_manager->blockSignals(true);
            QVariantMap data;
            data.insert(UDISKS2_JOB_KEY_OPERATION, block->mountPath().isEmpty() ? UDISKS2_JOB_OP_FS_UNMOUNT : UDISKS2_JOB_OP_FS_MOUNT);
            data.insert(UDISKS2_JOB_KEY_OBJECTS, QStringList() << block->path());
            qCInfo(lcMemoryCardLog) << "New partition status:" << data;
            UDisks2::Job tmpJob(QString(), data);
            tmpJob.complete(true);
            updatePartitionStatus(&tmpJob, true);
            m_manager->blockSignals(false);

            updatePartitionProperties(block);

            if (!m_operationQueue.isEmpty()) {
                Operation op = m_operationQueue.head();
                if (op.command == QStringLiteral("format") && block->mountPath().isEmpty()) {
                    m_operationQueue.dequeue();
                    doFormat(op.deviceName, op.dbusObjectPath, op.type, op.arguments);
                }
            }
        });
    }
}

void UDisks2::Monitor::doFormat(const QString &deviceName, const QString &dbusObjectPath, const QString &type, const QVariantHash &arguments)
{
    QDBusInterface blockDeviceInterface(UDISKS2_SERVICE,
                                    dbusObjectPath,
                                    UDISKS2_BLOCK_INTERFACE,
                                    QDBusConnection::systemBus());

    QDBusPendingCall pendingCall = blockDeviceInterface.asyncCall(UDISKS2_BLOCK_FORMAT, type, arguments);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, [this, deviceName, arguments](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            emit status(deviceName, Partition::Formatted);
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            QByteArray errorData = error.name().toLocal8Bit();
            const char *errorCStr = errorData.constData();
            qCWarning(lcMemoryCardLog) << "Format error:" << errorCStr;

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
            for (const QDBusObjectPath &dbusObjectPath : blockDevicePaths) {
                QString path = dbusObjectPath.path();
                if (externalBlockDevice(path)) {
                    createBlockDevice(path, InterfaceAndPropertyMap());
                }
            }
        } else if (watcher->isError()) {
            QDBusError error = watcher->error();
            qCWarning(lcMemoryCardLog) << "Unable to enumerate block devices:" << error.name() << error.message();
        }
    });
}

QString UDisks2::Monitor::objectPath(const QString &deviceName) const
{
    QString dev = QString("/dev/%1").arg(deviceName);

    for (QMap<QString, Block *>::const_iterator i = m_blockDevices.begin(); i != m_blockDevices.end(); ++i) {
        Block *block = i.value();
        if (block->device() == dev) {
            return block->path();
        }
    }

    return QString();
}
