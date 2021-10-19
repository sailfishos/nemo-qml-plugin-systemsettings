#include "udisks2block_p.h"
#include "udisks2defines.h"
#include "logging_p.h"

#include <nemo-dbus/dbus.h>
#include <nemo-dbus/interface.h>

UDisks2::Block::Block(const QString &path, const UDisks2::InterfacePropertyMap &interfacePropertyMap, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_interfacePropertyMap(interfacePropertyMap)
    , m_data(interfacePropertyMap.value(UDISKS2_BLOCK_INTERFACE))
    , m_connection(QDBusConnection::systemBus(), lcMemoryCardDBusLog())
    , m_mountable(interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE))
    , m_encrypted(interfacePropertyMap.contains(UDISKS2_ENCRYPTED_INTERFACE))
    , m_formatting(false)
    , m_locking(false)
{
    if (!m_connection.connectToSignal(
                UDISKS2_SERVICE,
                m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << m_path
                                   << m_connection.connection().lastError().message();
    }

    qCInfo(lcMemoryCardLog) << "Creating a new block. Mountable:" << m_mountable << ", encrypted:" << m_encrypted
                            << "object path:" << m_path << "data is empty:" << m_data.isEmpty();

    if (m_interfacePropertyMap.isEmpty()) {
        // Encrypted interface
        getProperties(
                m_path, UDISKS2_ENCRYPTED_INTERFACE, &m_pendingEncrypted,
                [this](const QVariantMap &encryptedProperties) {
                    m_encrypted = true;
                    m_interfacePropertyMap.insert(UDISKS2_ENCRYPTED_INTERFACE, encryptedProperties);
                });

        // File system interface
        getProperties(
                m_path, UDISKS2_FILESYSTEM_INTERFACE, &m_pendingFileSystem,
                [this](const QVariantMap &filesystemProperties) {
                    updateFileSystemInterface(filesystemProperties);
                });

        // Partition table interface
        getProperties(
                m_path, UDISKS2_PARTITION_TABLE_INTERFACE, &m_pendingPartitionTable,
                [this](const QVariantMap &partitionTableProperties) {
                    m_interfacePropertyMap.insert(UDISKS2_PARTITION_TABLE_INTERFACE, partitionTableProperties);
                });

        // Partition interface
        getProperties(
                m_path, UDISKS2_PARTITION_INTERFACE, &m_pendingPartition,
                [this](const QVariantMap &partitionProperties) {
                    m_interfacePropertyMap.insert(UDISKS2_PARTITION_INTERFACE, partitionProperties);
                });

        // Block interface
        getProperties(
                m_path, UDISKS2_BLOCK_INTERFACE, &m_pendingBlock,
                [this](const QVariantMap &blockProperties) {
                    qCInfo(lcMemoryCardLog) << "Block properties:" << blockProperties;
                    m_data = blockProperties;
                    m_interfacePropertyMap.insert(UDISKS2_BLOCK_INTERFACE, blockProperties);

                    // Drive path is blocks property => doing it the callback.
                    getProperties(
                            drive(), UDISKS2_DRIVE_INTERFACE, &m_pendingDrive,
                            [this](const QVariantMap &driveProperties) {
                                qCInfo(lcMemoryCardLog) << "Drive properties:" << driveProperties;
                                m_drive = driveProperties;
                            });
                });
    } else {
        if (m_mountable) {
            QVariantMap map = interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE);
            updateFileSystemInterface(map);
        }

        getProperties(
                drive(), UDISKS2_DRIVE_INTERFACE, &m_pendingDrive,
                [this](const QVariantMap &driveProperties) {
                    qCInfo(lcMemoryCardLog) << "Drive properties:" << driveProperties;
                    m_drive = driveProperties;
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
}

QString UDisks2::Block::path() const
{
    return m_path;
}

QString UDisks2::Block::device() const
{
    QByteArray d = m_data.value(QStringLiteral("Device")).toByteArray();
    return QString::fromLocal8Bit(d);
}

QString UDisks2::Block::preferredDevice() const
{
    QByteArray d = m_data.value(QStringLiteral("PreferredDevice")).toByteArray();
    return QString::fromLocal8Bit(d);
}

QString UDisks2::Block::drive() const
{
    return value(QStringLiteral("Drive")).toString();
}

QString UDisks2::Block::driveModel() const
{
    return NemoDBus::demarshallDBusArgument(m_drive.value(QStringLiteral("Model"))).toString();
}

QString UDisks2::Block::driveVendor() const
{
    return NemoDBus::demarshallDBusArgument(m_drive.value(QStringLiteral("Vendor"))).toString();
}

QString UDisks2::Block::connectionBus() const
{
    QString bus = NemoDBus::demarshallDBusArgument(m_drive.value(QStringLiteral("ConnectionBus"))).toString();

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
    return NemoDBus::demarshallDBusArgument(m_interfacePropertyMap.value(UDISKS2_PARTITION_INTERFACE).value(QStringLiteral("Table"))).toString();
}

bool UDisks2::Block::isPartition() const
{
    return !m_interfacePropertyMap.value(UDISKS2_PARTITION_INTERFACE).isEmpty();
}

bool UDisks2::Block::isPartitionTable() const
{
    return !m_interfacePropertyMap.value(UDISKS2_PARTITION_TABLE_INTERFACE).isEmpty();
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
    return m_encrypted;
}

bool UDisks2::Block::setEncrypted(bool encrypted)
{
    if (m_encrypted != encrypted) {
        m_encrypted = encrypted;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isMountable() const
{
    return m_mountable;
}

bool UDisks2::Block::setMountable(bool mountable)
{
    if (m_mountable != mountable) {
        m_mountable = mountable;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isFormatting() const
{
    return m_formatting;
}

bool UDisks2::Block::setFormatting(bool formatting)
{
    if (m_formatting != formatting) {
        m_formatting = formatting;
        emit updated();
        return true;
    }
    return false;
}

bool UDisks2::Block::isLocking() const
{
    return m_locking;
}

void UDisks2::Block::setLocking()
{
    m_locking = true;
}

bool UDisks2::Block::isReadOnly() const
{
    return value(QStringLiteral("ReadOnly")).toBool();
}

bool UDisks2::Block::hintAuto() const
{
    return value(QStringLiteral("HintAuto")).toBool();
}

bool UDisks2::Block::isValid() const
{
    bool hasBlock = m_interfacePropertyMap.contains(UDISKS2_BLOCK_INTERFACE);
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
    return m_mountPath;
}

QVariant UDisks2::Block::value(const QString &key) const
{
    return NemoDBus::demarshallDBusArgument(m_data.value(key));
}

bool UDisks2::Block::hasData() const
{
    return !m_data.isEmpty();
}

void UDisks2::Block::dumpInfo() const
{
    qCInfo(lcMemoryCardLog) << "Block device:" << device() << "Preferred device:" << preferredDevice();
    qCInfo(lcMemoryCardLog) << "- drive:" << drive() << "device number:" << deviceNumber() << "connection bus:" << connectionBus();
    qCInfo(lcMemoryCardLog) << "- id:" << id() << "size:" << size();
    qCInfo(lcMemoryCardLog) << "- isreadonly:" << isReadOnly() << "idtype:" << idType();
    qCInfo(lcMemoryCardLog) << "- idversion:" << idVersion() << "idlabel:" << idLabel();
    qCInfo(lcMemoryCardLog) << "- iduuid:" << idUUID();
    qCInfo(lcMemoryCardLog) << "- ismountable:" << isMountable() << "mount path:" << mountPath();
    qCInfo(lcMemoryCardLog) << "- isencrypted:" << isEncrypted() << "crypto backing device:" << cryptoBackingDevicePath()
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
    m_interfacePropertyMap.insert(interface, propertyMap);
    if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateFileSystemInterface(propertyMap);
    } else if (interface == UDISKS2_ENCRYPTED_INTERFACE) {
        setEncrypted(true);
    }
}

void UDisks2::Block::removeInterface(const QString &interface)
{
    m_interfacePropertyMap.remove(interface);
    if (interface == UDISKS2_BLOCK_INTERFACE) {
        m_data.clear();
    } else if (interface == UDISKS2_DRIVE_INTERFACE) {
        m_drive.clear();
    } else if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateFileSystemInterface(QVariantMap());
    } else if (interface == UDISKS2_ENCRYPTED_INTERFACE) {
        setEncrypted(false);
    }
}

int UDisks2::Block::interfaceCount() const
{
    return m_interfacePropertyMap.keys().count();
}

bool UDisks2::Block::hasInterface(const QString &interface) const
{
    return m_interfacePropertyMap.contains(interface);
}

void UDisks2::Block::morph(const UDisks2::Block &other)
{
    if (&other == this)
        return;

    if (!this->m_connection.connection().disconnect(
                UDISKS2_SERVICE,
                m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to disconnect to Block properties change interface" << m_path
                                   << m_connection.connection().lastError().message();
    }

    this->m_path = other.m_path;

    if (!this->m_connection.connectToSignal(
                UDISKS2_SERVICE,
                this->m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << m_path
                                   << m_connection.connection().lastError().message();
    }

    qCInfo(lcMemoryCardLog) << "Morphing" << qPrintable(device()) << "that was" << (m_formatting ? "formatting" : "not formatting" )
                            << "to" << qPrintable(other.device());
    qCInfo(lcMemoryCardLog) << "Old block:";
    dumpInfo();
    qCInfo(lcMemoryCardLog) << "New block:";
    other.dumpInfo();

    m_interfacePropertyMap = other.m_interfacePropertyMap;
    m_data = other.m_data;
    m_drive = other.m_drive;
    m_mountPath = other.m_mountPath;
    m_mountable = other.m_mountable;
    m_encrypted = other.m_encrypted;
    bool wasFormatting = m_formatting;
    m_formatting = other.m_formatting;
    m_locking = other.m_locking;


    if (wasFormatting && hasCryptoBackingDevice()) {
        rescan(cryptoBackingDeviceObjectPath());
    }
}

void UDisks2::Block::updateProperties(const QDBusMessage &message)
{
    QList<QVariant> arguments = message.arguments();
    QString interface = arguments.value(0).toString();
    if (interface == UDISKS2_BLOCK_INTERFACE) {
        QVariantMap changedProperties = NemoDBus::demarshallArgument<QVariantMap>(arguments.value(1));
        for (QMap<QString, QVariant>::const_iterator i = changedProperties.constBegin(); i != changedProperties.constEnd(); ++i) {
            m_data.insert(i.key(), i.value());
        }

        if (!clearFormattingState()) {
            emit updated();
        }
    } else if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateFileSystemInterface(arguments.value(1));
    }
}

bool UDisks2::Block::isCompleted() const
{
    return !m_pendingFileSystem && !m_pendingBlock && !m_pendingEncrypted && !m_pendingDrive
            && !m_pendingPartition && !m_pendingPartitionTable;
}

void UDisks2::Block::updateFileSystemInterface(const QVariant &filesystemInterface)
{
    QVariantMap filesystem = NemoDBus::demarshallArgument<QVariantMap>(filesystemInterface);

    bool interfaceChange = m_interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE) != filesystem.isEmpty();
    if (filesystem.isEmpty()) {
        m_interfacePropertyMap.remove(UDISKS2_FILESYSTEM_INTERFACE);
    } else {
        m_interfacePropertyMap.insert(UDISKS2_FILESYSTEM_INTERFACE, filesystem);
    }
    QList<QByteArray> mountPointList = NemoDBus::demarshallArgument<QList<QByteArray> >(filesystem.value(QStringLiteral("MountPoints")));
    m_mountPath.clear();

    if (!mountPointList.isEmpty()) {
        m_mountPath = QString::fromLocal8Bit(mountPointList.at(0));
    }

    bool triggerUpdate = false;
    blockSignals(true);
    triggerUpdate = setMountable(!filesystem.isEmpty());
    triggerUpdate |= clearFormattingState();
    triggerUpdate |= interfaceChange;
    blockSignals(false);

    if (triggerUpdate) {
        emit updated();
    }

    qCInfo(lcMemoryCardLog) << "New file system mount points:" << filesystemInterface
                            << "resolved mount path: " << m_mountPath << "trigger update:" << triggerUpdate;
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

    NemoDBus::Interface blockDeviceInterface(this, m_connection, UDISKS2_SERVICE, dbusObjectPath, UDISKS2_BLOCK_INTERFACE);
    NemoDBus::Response *response = blockDeviceInterface.call(UDISKS2_BLOCK_RESCAN, arguments);
    response->onError([this, dbusObjectPath](const QDBusError &error) {
        qCDebug(lcMemoryCardLog) << "UDisks failed to rescan object path" << dbusObjectPath
                                 << ", error type:" << error.type() << ", name:" << error.name() << ", message:" << error.message();
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

    NemoDBus::Interface dbusPropertyInterface(this, m_connection, UDISKS2_SERVICE, path, DBUS_OBJECT_PROPERTIES_INTERFACE);
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
