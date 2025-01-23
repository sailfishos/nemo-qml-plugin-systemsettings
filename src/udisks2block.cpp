#include "udisks2block_p.h"
#include "udisks2defines.h"
#include "logging_p.h"

#include <nemo-dbus/dbus.h>
#include <nemo-dbus/interface.h>
#include <nemo-dbus/connection.h>

class BlockPrivate
{
public:
    BlockPrivate(const QString &path, const UDisks2::InterfacePropertyMap &interfacePropertyMap)
        : m_path(path)
        , m_interfacePropertyMap(interfacePropertyMap)
        , m_data(interfacePropertyMap.value(UDISKS2_BLOCK_INTERFACE))
        , m_connection(QDBusConnection::systemBus(), lcMemoryCardDBusLog())
        , m_mountable(interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE))
        , m_encrypted(interfacePropertyMap.contains(UDISKS2_ENCRYPTED_INTERFACE))
    {}
    ~BlockPrivate() {}

    QString m_path;
    UDisks2::InterfacePropertyMap m_interfacePropertyMap;
    QVariantMap m_data;
    QVariantMap m_drive;
    NemoDBus::Connection m_connection;
    QString m_mountPath;
    bool m_mountable;
    bool m_encrypted;
    bool m_formatting = false;
    bool m_locking = false;

    bool m_overrideHintAuto = false;

    bool m_pendingFileSystem = false;
    bool m_pendingBlock = false;
    bool m_pendingEncrypted = false;
    bool m_pendingDrive = false;
    bool m_pendingPartition = false;
    bool m_pendingPartitionTable = false;
};

UDisks2::Block::Block(const QString &path, const UDisks2::InterfacePropertyMap &interfacePropertyMap, QObject *parent)
    : QObject(parent)
    , d_ptr(new BlockPrivate(path, interfacePropertyMap))
{
    if (!d_ptr->m_connection.connectToSignal(
                UDISKS2_SERVICE,
                d_ptr->m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << d_ptr->m_path
                                   << d_ptr->m_connection.connection().lastError().message();
    }

    qCInfo(lcMemoryCardLog) << "Creating a new block. Mountable:" << d_ptr->m_mountable
                            << ", encrypted:" << d_ptr->m_encrypted
                            << "object path:" << d_ptr->m_path << "data is empty:" << d_ptr->m_data.isEmpty();

    if (d_ptr->m_interfacePropertyMap.isEmpty()) {
        // Encrypted interface
        getProperties(
                d_ptr->m_path, UDISKS2_ENCRYPTED_INTERFACE, &d_ptr->m_pendingEncrypted,
                [this](const QVariantMap &encryptedProperties) {
                    d_ptr->m_encrypted = true;
                    d_ptr->m_interfacePropertyMap.insert(UDISKS2_ENCRYPTED_INTERFACE, encryptedProperties);
                });

        // File system interface
        getProperties(
                d_ptr->m_path, UDISKS2_FILESYSTEM_INTERFACE, &d_ptr->m_pendingFileSystem,
                [this](const QVariantMap &filesystemProperties) {
                    updateFileSystemInterface(filesystemProperties);
                });

        // Partition table interface
        getProperties(
                d_ptr->m_path, UDISKS2_PARTITION_TABLE_INTERFACE, &d_ptr->m_pendingPartitionTable,
                [this](const QVariantMap &partitionTableProperties) {
                    d_ptr->m_interfacePropertyMap.insert(UDISKS2_PARTITION_TABLE_INTERFACE, partitionTableProperties);
                });

        // Partition interface
        getProperties(
                d_ptr->m_path, UDISKS2_PARTITION_INTERFACE, &d_ptr->m_pendingPartition,
                [this](const QVariantMap &partitionProperties) {
                    d_ptr->m_interfacePropertyMap.insert(UDISKS2_PARTITION_INTERFACE, partitionProperties);
                });

        // Block interface
        getProperties(
                d_ptr->m_path, UDISKS2_BLOCK_INTERFACE, &d_ptr->m_pendingBlock,
                [this](const QVariantMap &blockProperties) {
                    qCInfo(lcMemoryCardLog) << "Block properties:" << blockProperties;
                    d_ptr->m_data = blockProperties;
                    d_ptr->m_interfacePropertyMap.insert(UDISKS2_BLOCK_INTERFACE, blockProperties);

                    // Drive path is blocks property => doing it the callback.
                    getProperties(
                            drive(), UDISKS2_DRIVE_INTERFACE, &d_ptr->m_pendingDrive,
                            [this](const QVariantMap &driveProperties) {
                                qCInfo(lcMemoryCardLog) << "Drive properties:" << driveProperties;
                                d_ptr->m_drive = driveProperties;
                            });
                });
    } else {
        if (d_ptr->m_mountable) {
            QVariantMap map = interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE);
            updateFileSystemInterface(map);
        }

        getProperties(
                drive(), UDISKS2_DRIVE_INTERFACE, &d_ptr->m_pendingDrive,
                [this](const QVariantMap &driveProperties) {
                    qCInfo(lcMemoryCardLog) << "Drive properties:" << driveProperties;
                    d_ptr->m_drive = driveProperties;
                });

        complete();
    }

    connect(this, &Block::completed, this, [this]() {
        clearFormattingState();
    });
}

UDisks2::Block &UDisks2::Block::operator=(const UDisks2::Block &)
{
    return *this;
}

UDisks2::Block::~Block()
{
    emit blockRemoved(device());
    delete d_ptr;
}

QString UDisks2::Block::path() const
{
    return d_ptr->m_path;
}

QString UDisks2::Block::device() const
{
    QByteArray d = d_ptr->m_data.value(QStringLiteral("Device")).toByteArray();
    return QString::fromLocal8Bit(d);
}

QString UDisks2::Block::preferredDevice() const
{
    QByteArray d = d_ptr->m_data.value(QStringLiteral("PreferredDevice")).toByteArray();
    return QString::fromLocal8Bit(d);
}

QString UDisks2::Block::drive() const
{
    return value(QStringLiteral("Drive")).toString();
}

QString UDisks2::Block::driveModel() const
{
    return NemoDBus::demarshallDBusArgument(d_ptr->m_drive.value(QStringLiteral("Model"))).toString();
}

QString UDisks2::Block::driveVendor() const
{
    return NemoDBus::demarshallDBusArgument(d_ptr->m_drive.value(QStringLiteral("Vendor"))).toString();
}

QString UDisks2::Block::connectionBus() const
{
    QString bus = NemoDBus::demarshallDBusArgument(d_ptr->m_drive.value(QStringLiteral("ConnectionBus"))).toString();

    // Do a bit of guesswork as we're missing connection between unlocked crypto block to crypto backing block device
    // from where we could see the drive where this block belongs to.
    if (bus != QLatin1String("/") && hasCryptoBackingDevice()) {
        QString cryptoBackingPath = cryptoBackingDevicePath();
        if (cryptoBackingPath.contains(QLatin1String("mmcblk"))) {
            return QStringLiteral("sdio");
        } else if (cryptoBackingPath.startsWith(QLatin1String("/dev/sd"))) {
            return QStringLiteral("usb");
        }
        return QStringLiteral("ieee1394");
    }

    return bus;
}

QString UDisks2::Block::partitionTable() const
{
    // Partion table that this partition belongs to.
    return NemoDBus::demarshallDBusArgument(d_ptr->m_interfacePropertyMap
                                            .value(UDISKS2_PARTITION_INTERFACE)
                                            .value(QStringLiteral("Table"))).toString();
}

bool UDisks2::Block::isPartition() const
{
    return !d_ptr->m_interfacePropertyMap.value(UDISKS2_PARTITION_INTERFACE).isEmpty();
}

bool UDisks2::Block::isPartitionTable() const
{
    return !d_ptr->m_interfacePropertyMap.value(UDISKS2_PARTITION_TABLE_INTERFACE).isEmpty();
}

qint64 UDisks2::Block::deviceNumber() const
{
    return value(QStringLiteral("DeviceNumber")).toLongLong();
}

QString UDisks2::Block::id() const
{
    return value(QStringLiteral("Id")).toString();
}

qint64 UDisks2::Block::size() const
{
    return value(QStringLiteral("Size")).toLongLong();
}

bool UDisks2::Block::isCryptoBlock() const
{
     return isEncrypted() || hasCryptoBackingDevice();
}

bool UDisks2::Block::hasCryptoBackingDevice() const
{
    const QString cryptoBackingDev = cryptoBackingDeviceObjectPath();
    return !cryptoBackingDev.isEmpty() && cryptoBackingDev != QLatin1String("/");
}

QString UDisks2::Block::cryptoBackingDevicePath() const
{
    const QString object = cryptoBackingDeviceObjectPath();
    return Block::cryptoBackingDevicePath(object);
}

QString UDisks2::Block::cryptoBackingDeviceObjectPath() const
{
    return value(UDisks2::cryptoBackingDeviceKey).toString();
}

bool UDisks2::Block::isEncrypted() const
{
    return d_ptr->m_encrypted;
}

bool UDisks2::Block::setEncrypted(bool encrypted)
{
    if (d_ptr->m_encrypted != encrypted) {
        d_ptr->m_encrypted = encrypted;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isMountable() const
{
    return d_ptr->m_mountable;
}

bool UDisks2::Block::setMountable(bool mountable)
{
    if (d_ptr->m_mountable != mountable) {
        d_ptr->m_mountable = mountable;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isFormatting() const
{
    return d_ptr->m_formatting;
}

bool UDisks2::Block::setFormatting(bool formatting)
{
    if (d_ptr->m_formatting != formatting) {
        d_ptr->m_formatting = formatting;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isLocking() const
{
    return d_ptr->m_locking;
}

void UDisks2::Block::setLocking()
{
    d_ptr->m_locking = true;
}

bool UDisks2::Block::isReadOnly() const
{
    return value(QStringLiteral("ReadOnly")).toBool();
}

bool UDisks2::Block::hintAuto() const
{
    return value(QStringLiteral("HintAuto")).toBool() || d_ptr->m_overrideHintAuto;
}

bool UDisks2::Block::isValid() const
{
    bool hasBlock = d_ptr->m_interfacePropertyMap.contains(UDISKS2_BLOCK_INTERFACE);
    if (hasBlock && device().startsWith(QStringLiteral("/dev/dm"))) {
        return hasCryptoBackingDevice();
    }
    return hasBlock;
}

QString UDisks2::Block::idType() const
{
    return value(QStringLiteral("IdType")).toString();
}

QString UDisks2::Block::idVersion() const
{
    return value(QStringLiteral("IdVersion")).toString();
}

QString UDisks2::Block::idLabel() const
{
    return value(QStringLiteral("IdLabel")).toString();
}

QString UDisks2::Block::idUUID() const
{
    return value(QStringLiteral("IdUUID")).toString();
}

QStringList UDisks2::Block::symlinks() const
{
    QStringList links;
    QVariant variantListBytes = value(QStringLiteral("Symlinks"));

    if (variantListBytes.canConvert<QVariantList>()) {
        QSequentialIterable iterable = variantListBytes.value<QSequentialIterable>();

        for (const QVariant &a : iterable) {
            QByteArray symlinkBytes;

            if (a.canConvert<QVariantList>()) {
                QSequentialIterable i = a.value<QSequentialIterable>();
                for (const QVariant &variantByte : i) {
                    symlinkBytes.append(variantByte.toChar());
                }
            }

            if (!symlinkBytes.isEmpty())
                links << QString::fromLocal8Bit(symlinkBytes);
        }
    }

    return links;
}

QString UDisks2::Block::mountPath() const
{
    return d_ptr->m_mountPath;
}

QVariant UDisks2::Block::value(const QString &key) const
{
    return NemoDBus::demarshallDBusArgument(d_ptr->m_data.value(key));
}

bool UDisks2::Block::hasData() const
{
    return !d_ptr->m_data.isEmpty();
}

void UDisks2::Block::dumpInfo() const
{
    qCInfo(lcMemoryCardLog) << this << ":" << device() << "Preferred device:" << preferredDevice()
                            << "D-Bus object path:" << path();
    qCInfo(lcMemoryCardLog) << "- drive:" << drive() << "device number:" << deviceNumber()
                            << "connection bus:" << connectionBus();
    qCInfo(lcMemoryCardLog) << "- id:" << id() << "size:" << size();
    qCInfo(lcMemoryCardLog) << "- isreadonly:" << isReadOnly() << "idtype:" << idType();
    qCInfo(lcMemoryCardLog) << "- idversion:" << idVersion() << "idlabel:" << idLabel();
    qCInfo(lcMemoryCardLog) << "- iduuid:" << idUUID();
    qCInfo(lcMemoryCardLog) << "- ismountable:" << isMountable() << "mount path:" << mountPath();
    qCInfo(lcMemoryCardLog) << "- isencrypted:" << isEncrypted()
                            << "crypto backing device:" << cryptoBackingDevicePath()
                            << "crypto backing object path:" << cryptoBackingDeviceObjectPath();
    qCInfo(lcMemoryCardLog) << "- isformatting:" << isFormatting();
    qCInfo(lcMemoryCardLog) << "- ispartiontable:" << isPartitionTable() << "ispartition:" << isPartition();
    qCInfo(lcMemoryCardLog) << "- hintAuto:" << hintAuto();
}

QString UDisks2::Block::cryptoBackingDevicePath(const QString &objectPath)
{
    if (objectPath == QLatin1String("/") || objectPath.isEmpty()) {
        return QString();
    } else {
        QString deviceName = objectPath.section(QChar('/'), 5);
        return QString("/dev/%1").arg(deviceName);
    }
}

void UDisks2::Block::addInterface(const QString &interface, QVariantMap propertyMap)
{
    d_ptr->m_interfacePropertyMap.insert(interface, propertyMap);
    if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateFileSystemInterface(propertyMap);
    } else if (interface == UDISKS2_ENCRYPTED_INTERFACE) {
        setEncrypted(true);
    }
}

void UDisks2::Block::removeInterface(const QString &interface)
{
    d_ptr->m_interfacePropertyMap.remove(interface);
    if (interface == UDISKS2_BLOCK_INTERFACE) {
        d_ptr->m_data.clear();
    } else if (interface == UDISKS2_DRIVE_INTERFACE) {
        d_ptr->m_drive.clear();
    } else if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateFileSystemInterface(QVariantMap());
    } else if (interface == UDISKS2_ENCRYPTED_INTERFACE) {
        setEncrypted(false);
    }
}

int UDisks2::Block::interfaceCount() const
{
    return d_ptr->m_interfacePropertyMap.keys().count();
}

bool UDisks2::Block::hasInterface(const QString &interface) const
{
    return d_ptr->m_interfacePropertyMap.contains(interface);
}

void UDisks2::Block::updateProperties(const QDBusMessage &message)
{
    QList<QVariant> arguments = message.arguments();
    QString interface = arguments.value(0).toString();
    if (interface == UDISKS2_BLOCK_INTERFACE) {
        QVariantMap changedProperties = NemoDBus::demarshallArgument<QVariantMap>(arguments.value(1));
        for (QMap<QString, QVariant>::const_iterator i = changedProperties.constBegin(); i != changedProperties.constEnd(); ++i) {
            d_ptr->m_data.insert(i.key(), i.value());
        }

        if (!clearFormattingState()) {
            emit updated();
        }
    } else if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        QVariantMap filesystemProperties = NemoDBus::demarshallArgument<QVariantMap>(arguments.value(1));
        if (!filesystemProperties.isEmpty())
            updateFileSystemInterface(filesystemProperties);

        QStringList invalidatedProperties = NemoDBus::demarshallArgument<QStringList>(arguments.value(2));
        if (invalidatedProperties.contains("MountPoints")) {
            // we are generally getting initial values and then tracking changes, assuming that
            // udisks2 passes the new values instead of just invalidating.
            // catch here at least if it does something unexpected.
            qWarning() << "FIXME: invalidated udisks2 filesystem properties contained MountPoints";
        }
    }
}

bool UDisks2::Block::isCompleted() const
{
    return !d_ptr->m_pendingFileSystem && !d_ptr->m_pendingBlock && !d_ptr->m_pendingEncrypted && !d_ptr->m_pendingDrive
            && !d_ptr->m_pendingPartition && !d_ptr->m_pendingPartitionTable;
}

// explicitly empty map as parameter clears the content
void UDisks2::Block::updateFileSystemInterface(const QVariantMap &filesystemProperties)
{
    bool interfaceChange = d_ptr->m_interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE) != filesystemProperties.isEmpty();
    d_ptr->m_mountPath.clear();

    if (filesystemProperties.isEmpty()) {
        d_ptr->m_interfacePropertyMap.remove(UDISKS2_FILESYSTEM_INTERFACE);
    } else {
        QVariantMap currentValues = d_ptr->m_interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE);

        for (QMap<QString, QVariant>::const_iterator i = filesystemProperties.constBegin()
             ; i != filesystemProperties.constEnd(); ++i) {
            currentValues.insert(i.key(), i.value());
        }

        d_ptr->m_interfacePropertyMap.insert(UDISKS2_FILESYSTEM_INTERFACE, currentValues);

        QList<QByteArray> mountPointList = NemoDBus::demarshallArgument<QList<QByteArray> >(
                    filesystemProperties.value(QStringLiteral("MountPoints")));

        if (!mountPointList.isEmpty()) {
            d_ptr->m_mountPath = QString::fromLocal8Bit(mountPointList.at(0));
        }
    }

    bool triggerUpdate = false;
    blockSignals(true);
    triggerUpdate = setMountable(!filesystemProperties.isEmpty());
    triggerUpdate |= clearFormattingState();
    triggerUpdate |= interfaceChange;
    blockSignals(false);

    if (triggerUpdate) {
        emit updated();
    }

    qCInfo(lcMemoryCardLog) << "New file system mount points:" << filesystemProperties
                            << "resolved mount path: " << d_ptr->m_mountPath << "trigger update:" << triggerUpdate;
    emit mountPathChanged();
}

void UDisks2::Block::complete()
{
    if (isCompleted()) {
        QMetaObject::invokeMethod(this, "completed", Qt::QueuedConnection);
    }
}

bool UDisks2::Block::clearFormattingState()
{
    if (isCompleted() && isMountable() && isFormatting()) {
        return setFormatting(false);
    }
    return false;
}

void UDisks2::Block::rescan(const QString &dbusObjectPath)
{
    QVariantList arguments;
    QVariantMap options;
    arguments << options;

    NemoDBus::Interface blockDeviceInterface(this, d_ptr->m_connection,
                                             UDISKS2_SERVICE, dbusObjectPath, UDISKS2_BLOCK_INTERFACE);
    NemoDBus::Response *response = blockDeviceInterface.call(UDISKS2_BLOCK_RESCAN, arguments);
    response->onError([this, dbusObjectPath](const QDBusError &error) {
        qCDebug(lcMemoryCardLog) << "UDisks failed to rescan object path" << dbusObjectPath
                                 << ", error type:" << error.type() << ", name:" << error.name()
                                 << ", message:" << error.message();
    });
}

void UDisks2::Block::getProperties(const QString &path, const QString &interface,
                                   bool *pending,
                                   std::function<void (const QVariantMap &)> success,
                                   std::function<void ()> failed)
{
    if (path.isEmpty() || path == QLatin1String("/")) {
        qCInfo(lcMemoryCardLog) << "Ignoring get properties from path:" << path << "interface:" << interface;
        return;
    }

    *pending = true;

    NemoDBus::Interface dbusPropertyInterface(this, d_ptr->m_connection,
                                              UDISKS2_SERVICE, path, DBUS_OBJECT_PROPERTIES_INTERFACE);
    NemoDBus::Response *response = dbusPropertyInterface.call(DBUS_GET_ALL, interface);
    response->onFinished<QVariantMap>([this, success](const QVariantMap &values) {
        success(NemoDBus::demarshallArgument<QVariantMap>(values));
    });
    response->onError([this, failed, path, interface](const QDBusError &error) {
        qCDebug(lcMemoryCardLog) << "Get properties failed" << path << "interface:" << interface;
        qCDebug(lcMemoryCardLog) << "Error reading" << interface << "properties:" << error.name() << error.message();
        failed();
    });
    connect(response, &QObject::destroyed, this, [this, pending] {
        *pending = false;
        complete();
    });
}
