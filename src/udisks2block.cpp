#include "udisks2block_p.h"
#include "udisks2defines.h"
#include "logging_p.h"

#include <nemo-dbus/dbus.h>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>

UDisks2::Block::Block(const QString &path, const QVariantMap &data, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_data(data)
    , m_connection(QDBusConnection::systemBus())
{
    if (!m_connection.connect(
                UDISKS2_SERVICE,
                m_path,
                DBUS_OBJECT_PROPERTIES_INTERFACE,
                QStringLiteral("PropertiesChanged"),
                this,
                SLOT(updateProperties(QDBusMessage)))) {
        qCWarning(lcMemoryCardLog) << "Failed to connect to Block properties change interface" << m_path << m_connection.lastError().message();
    }

    QDBusInterface dbusPropertyInterface(UDISKS2_SERVICE,
                                    m_path,
                                    DBUS_OBJECT_PROPERTIES_INTERFACE,
                                    m_connection);
    QDBusPendingCall pendingCall = dbusPropertyInterface.asyncCall(DBUS_GET_ALL, UDISKS2_FILESYSTEM_INTERFACE);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
        if (watcher->isValid() && watcher->isFinished()) {
            QDBusPendingReply<> reply =  *watcher;
            QDBusMessage message = reply.reply();
            updateMountPoint(message.arguments().at(0));
        } else {
            QDBusError error = watcher->error();
            qCWarning(lcMemoryCardLog) << "Error reading filesystem properties:" << error.name() << error.message();
        }
        watcher->deleteLater();
    });

    if (data.isEmpty()) {
        pendingCall = dbusPropertyInterface.asyncCall(DBUS_GET_ALL, UDISKS2_BLOCK_INTERFACE);
        watcher = new QDBusPendingCallWatcher(pendingCall, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *watcher) {
            if (watcher->isValid() && watcher->isFinished()) {
                QDBusPendingReply<> reply =  *watcher;
                QDBusMessage message = reply.reply();
                QVariantMap blockProperties = NemoDBus::demarshallArgument<QVariantMap>(message.arguments().at(0));
                qCInfo(lcMemoryCardLog) << "Block properties:" << blockProperties;
                m_data = blockProperties;
                emit blockUpdated();
            } else {
                QDBusError error = watcher->error();
                qCWarning(lcMemoryCardLog) << "Error reading block properties:" << error.name() << error.message();
            }
            watcher->deleteLater();
        });
    }
}

UDisks2::Block::~Block()
{
}

QString UDisks2::Block::path() const
{
    return m_path;
}

QString UDisks2::Block::cryptoBackingDevice() const
{
    return m_data.value(QStringLiteral("CryptoBackingDevice")).value<QDBusObjectPath>().path();
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

bool UDisks2::Block::isReadOnly() const
{
    return value(QStringLiteral("ReadOnly")).toBool();
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

void UDisks2::Block::updateProperties(const QDBusMessage &message)
{
    QList<QVariant> arguments = message.arguments();
    QString interface = arguments.value(0).toString();
    if (interface == UDISKS2_BLOCK_INTERFACE) {
        QVariantMap changedProperties = NemoDBus::demarshallArgument<QVariantMap>(arguments.value(1));
        qCInfo(lcMemoryCardLog) << "Changed properties:" << changedProperties;
        for (QMap<QString, QVariant>::const_iterator i = changedProperties.begin(); i != changedProperties.end(); ++i) {
            m_data.insert(i.key(), i.value());
        }
        emit blockUpdated();
    } else if (interface == UDISKS2_FILESYSTEM_INTERFACE) {
        updateMountPoint(arguments.value(1));
    }
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

    qCInfo(lcMemoryCardLog) << "New file system mount points:" << mountPoints << "resolved mount path: " << m_mountPath;
    emit mountPathChanged();
}
