#include "udisks2block_p.h"
#include "udisks2defines.h"
#include "logging_p.h"

#include <nemo-dbus/dbus.h>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

UDisks2::Block::Block(const QString &path, const UDisks2::InterfacePropertyMap &interfacePropertyMap, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_interfacePropertyMap(interfacePropertyMap)
    , m_data(interfacePropertyMap.value(UDISKS2_BLOCK_INTERFACE))
    , m_connection(QDBusConnection::systemBus())
    , m_mountable(interfacePropertyMap.contains(UDISKS2_FILESYSTEM_INTERFACE))
    , m_encrypted(interfacePropertyMap.contains(UDISKS2_ENCRYPTED_INTERFACE))
    , m_formatting(false)
    , m_locking(false)
    , m_pendingFileSystem(nullptr)
    , m_pendingBlock(nullptr)
    , m_pendingEncrypted(nullptr)
{
    if (!m_connection.connect(
                UDISKS2_SERVICE,
                m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << m_path << m_connection.lastError().message();
    }

    QDBusInterface dbusPropertyInterface(UDISKS2_SERVICE,
                                    m_path,
                                    DBUS_OBJECT_PROPERTIES_INTERFACE,
                                    m_connection);

    qCInfo(lcMemoryCardLog) << "Creating a new block. Mountable:" << m_mountable << ", encrypted:" << m_encrypted << "object path:" << m_path << "data is empty:" << m_data.isEmpty();

    if (m_data.isEmpty()) {
        getFileSystemInterface();
        getEncryptedInterface();
        QDBusPendingCall pendingCall = dbusPropertyInterface.asyncCall(DBUS_GET_ALL, UDISKS2_BLOCK_INTERFACE);
        m_pendingBlock = new QDBusPendingCallWatcher(pendingCall, this);
        connect(m_pendingBlock, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            if (watcher->isValid() && watcher->isFinished()) {
                QDBusPendingReply<> reply =  *watcher;
                QDBusMessage message = reply.reply();
                QVariantMap blockProperties = NemoDBus::demarshallArgument<QVariantMap>(message.arguments().at(0));
                qCInfo(lcMemoryCardLog) << "Block properties:" << blockProperties;
                m_data = blockProperties;
            } else {
                QDBusError error = watcher->error();
                qCWarning(lcMemoryCardLog) << "Error reading block properties:" << error.name() << error.message();
            }
            m_pendingBlock->deleteLater();
            m_pendingBlock = nullptr;
            complete();
        });
    } else {
        if (m_mountable) {
            QVariantMap map = interfacePropertyMap.value(UDISKS2_FILESYSTEM_INTERFACE);
            updateMountPoint(map);
        }

        // We have either org.freedesktop.UDisks2.Filesystem or org.freedesktop.UDisks2.Encrypted interface.
        complete();
    }

    connect(this, &Block::completed, this, [this]() {
        clearFormattingState();
    });
}

// Use when morphing a block e.g. updating encrypted block to crypto backing block device (e.i. to a block that implements file system).
UDisks2::Block &UDisks2::Block::operator=(const UDisks2::Block &other)
{
    if (&other == this)
        return *this;

    if (!this->m_connection.disconnect(
                UDISKS2_SERVICE,
                m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to disconnect to Block properties change interface" << m_path << m_connection.lastError().message();
    }

    this->m_path = other.m_path;

    if (!this->m_connection.connect(
                UDISKS2_SERVICE,
                this->m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                UDisks2::propertiesChangedSignal,
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << m_path << m_connection.lastError().message();
    }

    m_interfacePropertyMap = other.m_interfacePropertyMap;
    m_data = other.m_data;
    m_mountable = other.m_mountable;
    m_mountPath = other.m_mountPath;
    m_encrypted = other.m_encrypted;
    m_formatting = other.m_formatting;
    m_locking = other.m_locking;

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

bool UDisks2::Block::hasCryptoBackingDevice() const
{
    const QString cryptoBackingDev = cryptoBackingDeviceObjectPath();
    return cryptoBackingDev != QLatin1String("/");
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

bool UDisks2::Block::isExternal() const
{
    const QString prefDevice = preferredDevice();
    return prefDevice != QStringLiteral("/dev/sailfish/home") && prefDevice != QStringLiteral("/dev/sailfish/root");
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
    qCInfo(lcMemoryCardLog) << "- drive:" << drive() << "dNumber:" << deviceNumber();
    qCInfo(lcMemoryCardLog) << "- id:" << id() << "size:" << size();
    qCInfo(lcMemoryCardLog) << "- isreadonly:" << isReadOnly() << "idtype:" << idType();
    qCInfo(lcMemoryCardLog) << "- idversion:" << idVersion() << "idlabel:" << idLabel();
    qCInfo(lcMemoryCardLog) << "- iduuid:" << idUUID();
    qCInfo(lcMemoryCardLog) << "- ismountable:" << isMountable() << "mount path:" << mountPath();
    qCInfo(lcMemoryCardLog) << "- isencrypted:" << isEncrypted() << "crypto backing device:" << cryptoBackingDevicePath();
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
        updateMountPoint(arguments.value(1));
    }
}

bool UDisks2::Block::isCompleted() const
{
    return !m_pendingFileSystem && !m_pendingBlock && !m_pendingEncrypted;
}

void UDisks2::Block::updateMountPoint(const QVariant &mountPoints)
{
    QVariantMap mountPointsMap = NemoDBus::demarshallArgument<QVariantMap>(mountPoints);
    QList<QByteArray> mountPointList = NemoDBus::demarshallArgument<QList<QByteArray> >(mountPointsMap.value(QStringLiteral("MountPoints")));
    m_mountPath.clear();

    for (const QByteArray &bytes : mountPointList) {
        if (bytes.startsWith("/run")) {
            m_mountPath = QString::fromLocal8Bit(bytes);
            break;
        }
    }

    bool triggerUpdate = false;
    blockSignals(true);
    triggerUpdate = setMountable(true);
    triggerUpdate |= clearFormattingState();
    blockSignals(false);

    if (triggerUpdate) {
        emit updated();
    }

    qCInfo(lcMemoryCardLog) << "New file system mount points:" << mountPoints << "resolved mount path: " << m_mountPath << "trigger update:" << triggerUpdate;
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

void UDisks2::Block::getFileSystemInterface()
{
    QDBusInterface dbusPropertyInterface(UDISKS2_SERVICE,
                                    m_path,
                                    DBUS_OBJECT_PROPERTIES_INTERFACE,
                                    m_connection);
    QDBusPendingCall pendingCall = dbusPropertyInterface.asyncCall(DBUS_GET_ALL, UDISKS2_FILESYSTEM_INTERFACE);
    m_pendingFileSystem = new QDBusPendingCallWatcher(pendingCall, this);
    connect(m_pendingFileSystem, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            QDBusPendingReply<> reply =  *watcher;
            QDBusMessage message = reply.reply();
            updateMountPoint(message.arguments().at(0));
        } else {
            QDBusError error = watcher->error();
            qCWarning(lcMemoryCardLog) << "Error reading filesystem properties:" << error.name() << error.message() << m_path;
            m_mountable = false;
        }
        m_pendingFileSystem->deleteLater();
        m_pendingFileSystem = nullptr;
        complete();
    });
}

void UDisks2::Block::getEncryptedInterface()
{
    QDBusInterface dbusPropertyInterface(UDISKS2_SERVICE,
                                    m_path,
                                    DBUS_OBJECT_PROPERTIES_INTERFACE,
                                    m_connection);
    QDBusPendingCall pendingCall = dbusPropertyInterface.asyncCall(DBUS_GET_ALL, UDISKS2_ENCRYPTED_INTERFACE);
    m_pendingEncrypted = new QDBusPendingCallWatcher(pendingCall, this);
    connect(m_pendingEncrypted, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            m_encrypted = true;
        } else {
            QDBusError error = watcher->error();
            qCWarning(lcMemoryCardLog) << "Error reading encrypted properties:" << error.name() << error.message() << m_path;
            m_encrypted = false;
        }
        m_pendingEncrypted->deleteLater();
        m_pendingEncrypted = nullptr;
        complete();
    });
}
