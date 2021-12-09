#ifndef NFCSETTINGS_H
#define NFCSETTINGS_H

#include <systemsettingsglobal.h>

#include <QObject>
#include <QTimer>

#include <nemo-dbus/interface.h>

class SYSTEMSETTINGS_EXPORT NfcSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit NfcSettings(QObject *parent = nullptr);
    ~NfcSettings();

    bool valid() const;
    bool available() const;
    bool enabled() const;
    void setEnabled(bool enabled);

signals:
    void validChanged();
    void availableChanged();
    void enabledChanged();

private:
    bool m_valid;
    bool m_enabled;
    bool m_available;
    NemoDBus::Interface m_interface;
    QTimer *m_timer;

private slots:
    void updateEnabledState(bool enabled);
};

#endif // NFCSETTINGS_H
