/*
 * Copyright (C) 2013 Jolla Ltd. <pekka.vuorela@jollamobile.com>
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

#ifndef PROFILECONTROL_H
#define PROFILECONTROL_H

#include <QObject>
#include <QString>
#include <QStringList>

class ProfileControl: public QObject
{
    Q_OBJECT
    Q_ENUMS(VibraMode)
    Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
    Q_PROPERTY(int ringerVolume READ ringerVolume WRITE setRingerVolume NOTIFY ringerVolumeChanged())
    Q_PROPERTY(int vibraMode READ vibraMode WRITE setVibraMode NOTIFY vibraModeChanged())
    Q_PROPERTY(int systemSoundLevel READ systemSoundLevel WRITE setSystemSoundLevel NOTIFY systemSoundLevelChanged)
    Q_PROPERTY(int touchscreenToneLevel READ touchscreenToneLevel WRITE setTouchscreenToneLevel NOTIFY touchscreenToneLevelChanged)
    Q_PROPERTY(int touchscreenVibrationLevel READ touchscreenVibrationLevel WRITE setTouchscreenVibrationLevel NOTIFY touchscreenVibrationLevelChanged)

    Q_PROPERTY(QString ringerToneFile READ ringerToneFile WRITE setRingerToneFile NOTIFY ringerToneFileChanged)
    Q_PROPERTY(QString messageToneFile READ messageToneFile WRITE setMessageToneFile NOTIFY messageToneFileChanged)
    Q_PROPERTY(QString chatToneFile READ chatToneFile WRITE setChatToneFile NOTIFY chatToneFileChanged)
    Q_PROPERTY(QString mailToneFile READ mailToneFile WRITE setMailToneFile NOTIFY mailToneFileChanged)
    Q_PROPERTY(QString internetCallToneFile READ internetCallToneFile WRITE setInternetCallToneFile NOTIFY internetCallToneFileChanged)
    Q_PROPERTY(QString calendarToneFile READ calendarToneFile WRITE setCalendarToneFile NOTIFY calendarToneFileChanged)
    Q_PROPERTY(QString clockAlarmToneFile READ clockAlarmToneFile WRITE setClockAlarmToneFile NOTIFY clockAlarmToneFileChanged)

    Q_PROPERTY(bool ringerToneEnabled READ ringerToneEnabled WRITE setRingerToneEnabled NOTIFY ringerToneEnabledChanged)
    Q_PROPERTY(bool messageToneEnabled READ messageToneEnabled WRITE setMessageToneEnabled NOTIFY messageToneEnabledChanged)
    Q_PROPERTY(bool chatToneEnabled READ chatToneEnabled WRITE setChatToneEnabled NOTIFY chatToneEnabledChanged)
    Q_PROPERTY(bool mailToneEnabled READ mailToneEnabled WRITE setMailToneEnabled NOTIFY mailToneEnabledChanged)
    Q_PROPERTY(bool internetCallToneEnabled READ internetCallToneEnabled WRITE setInternetCallToneEnabled NOTIFY internetCallToneEnabledChanged)
    Q_PROPERTY(bool calendarToneEnabled READ calendarToneEnabled WRITE setCalendarToneEnabled NOTIFY calendarToneEnabledChanged)
    Q_PROPERTY(bool clockAlarmToneEnabled READ clockAlarmToneEnabled WRITE setClockAlarmToneEnabled NOTIFY clockAlarmToneEnabledChanged)

public:
    enum VibraMode {
        VibraAlways,
        VibraSilent,
        VibraNormal,
        VibraNever
    };

    /*!
     * Register the callback functions with libprofile and
     * activate profile change tracking.
     *
     * \param parent the parent object
     */
    ProfileControl(QObject *parent = 0);

    /*!
     * Unregisters the callback functions from libprofile and
     * deactivates profile change tracking.
     */
    virtual ~ProfileControl();

    /*!
     * Returns the name of the current profile.
     *
     * \return the current profile
     */
    QString profile();

    /*!
     * Sets the current profile.
     *
     * \param profile the name of the profile to set.
     */
    void setProfile(const QString &profile);

    int ringerVolume() const;
    void setRingerVolume(int volume);

    int vibraMode() const;
    void setVibraMode(int mode);

    int systemSoundLevel();
    void setSystemSoundLevel(int level);

    int touchscreenToneLevel();
    void setTouchscreenToneLevel(int level);

    int touchscreenVibrationLevel();
    void setTouchscreenVibrationLevel(int level);

    QString ringerToneFile();
    void setRingerToneFile(const QString &filename);

    int ringerToneVolume();
    void setRingerToneVolume(int volume);

    QString messageToneFile();
    void setMessageToneFile(const QString &filename);

    QString chatToneFile();
    void setChatToneFile(const QString &filename);

    QString mailToneFile();
    void setMailToneFile(const QString &filename);

    QString internetCallToneFile();
    void setInternetCallToneFile(const QString &filename);

    QString calendarToneFile();
    void setCalendarToneFile(const QString &filename);

    QString clockAlarmToneFile();
    void setClockAlarmToneFile(const QString &filename);

    bool ringerToneEnabled();
    void setRingerToneEnabled(bool enabled);

    bool messageToneEnabled();
    void setMessageToneEnabled(bool enabled);

    bool chatToneEnabled();
    void setChatToneEnabled(bool enabled);

    bool mailToneEnabled();
    void setMailToneEnabled(bool enabled);

    bool internetCallToneEnabled();
    void setInternetCallToneEnabled(bool enabled);

    bool calendarToneEnabled();
    void setCalendarToneEnabled(bool enabled);

    bool clockAlarmToneEnabled();
    void setClockAlarmToneEnabled(bool enabled);


signals:
    /*!
     * Signal that the profile has changed.
     *
     * \param profile The profile that has been selected as the current profile
     */
    void profileChanged(const QString &profile);
    void ringerVolumeChanged();
    void vibraModeChanged();

    void systemSoundLevelChanged();
    void touchscreenToneLevelChanged();
    void touchscreenVibrationLevelChanged();

    void ringerToneFileChanged();
    void messageToneFileChanged();
    void messageToneVolumeChanged();
    void internetCallToneFileChanged();
    void chatToneFileChanged();
    void mailToneFileChanged();
    void calendarToneFileChanged();
    void clockAlarmToneFileChanged();

    void ringerToneEnabledChanged();
    void messageToneEnabledChanged();
    void chatToneEnabledChanged();
    void mailToneEnabledChanged();
    void internetCallToneEnabledChanged();
    void calendarToneEnabledChanged();
    void clockAlarmToneEnabledChanged();

private:
    static int s_instanceCounter;

    QString m_profile;
    int m_ringerVolume;
    bool m_vibraInGeneral;
    bool m_vibraInSilent;
    int m_systemSoundLevel;
    int m_touchscreenToneLevel;
    int m_touchscreenVibrationLevel;
    QString m_ringerToneFile;
    QString m_messageToneFile;
    QString m_internetCallToneFile;
    QString m_chatToneFile;
    QString m_mailToneFile;
    QString m_calendarToneFile;
    QString m_clockAlarmToneFile;

    int m_ringerToneEnabled;
    int m_messageToneEnabled;
    int m_chatToneEnabled;
    int m_mailToneEnabled;
    int m_internetCallToneEnabled;
    int m_calendarToneEnabled;
    int m_clockAlarmToneEnabled;

    //! libprofile callback for profile changes
    static void currentProfileChangedCallback(const char *profile, ProfileControl *profileControl);

    //! libprofile callback for property changes
    static void updateStateCallBackTrampoline(const char *profile, const char *key, const char *val, const char *type,
                                    ProfileControl *profileControl);
    void updateStateCallBack(const char *profile, const char *key, const char *val, const char *type);
};

#endif
