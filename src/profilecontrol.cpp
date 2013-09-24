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

#include <libprofile.h>
#include "profilecontrol.h"
#include <QDebug>

// NOTE: most of profiled interface blocks


const char * const VolumeKey = "ringing.alert.volume";
const char * const VibraKey = "vibrating.alert.enabled";
const char * const SystemSoundLevelKey = "system.sound.level";
const char * const TouchscreenToneLevelKey = "touchscreen.sound.level";
const char * const TouchscreenVibrationLevelKey = "touchscreen.vibration.level";

const char * const RingerToneKey = "ringing.alert.tone";
const char * const MessageToneKey ="sms.alert.tone";
const char * const ChatToneKey ="im.alert.tone";
const char * const MailToneKey ="email.alert.tone";
const char * const InternetCallToneKey ="voip.alert.tone";
const char * const CalendarToneKey ="calendar.alert.tone";
const char * const ClockAlarmToneKey ="clock.alert.tone";

const char * const RingerToneEnabledKey = "ringing.alert.enabled";
const char * const MessageToneEnabledKey ="sms.alert.enabled";
const char * const ChatToneEnabledKey ="im.alert.enabled";
const char * const MailToneEnabledKey ="email.alert.enabled";
const char * const InternetCallToneEnabledKey ="voip.alert.enabled";
const char * const CalendarToneEnabledKey ="calendar.alert.enabled";
const char * const ClockAlarmToneEnabledKey ="clock.alert.enabled";

const char * const GeneralProfile = "general";
const char * const SilentProfile = "silent";

int ProfileControl::s_instanceCounter = 0;


ProfileControl::ProfileControl(QObject *parent)
    : QObject(parent),
      m_systemSoundLevel(-1),
      m_touchscreenToneLevel(-1),
      m_touchscreenVibrationLevel(-1),
      m_ringerToneEnabled(-1),
      m_messageToneEnabled(-1),
      m_chatToneEnabled(-1),
      m_mailToneEnabled(-1),
      m_internetCallToneEnabled(-1),
      m_calendarToneEnabled(-1),
      m_clockAlarmToneEnabled(-1)
{
    profile_track_add_profile_cb((profile_track_profile_fn_data) currentProfileChangedCallback, this, NULL);

    // track changes in active and inactive profile(s)
    profile_track_add_active_cb((profile_track_value_fn_data) &updateStateCallBackTrampoline, this, NULL);
    profile_track_add_change_cb((profile_track_value_fn_data) &updateStateCallBackTrampoline, this, NULL);

    profile_connection_enable_autoconnect();

    if (s_instanceCounter == 0) {
        profile_tracker_init();
    }
    s_instanceCounter++;

    m_ringerVolume = profile_get_value_as_int(GeneralProfile, VolumeKey);
    m_vibraInGeneral = profile_get_value_as_bool(GeneralProfile, VibraKey);
    m_vibraInSilent = profile_get_value_as_bool(SilentProfile, VibraKey);
}

ProfileControl::~ProfileControl()
{
    s_instanceCounter--;
    if (s_instanceCounter == 0) {
        profile_tracker_quit();
    }

    profile_track_remove_profile_cb((profile_track_profile_fn_data) currentProfileChangedCallback, this);
    profile_track_remove_active_cb((profile_track_value_fn_data) &updateStateCallBackTrampoline, this);
    profile_track_remove_change_cb((profile_track_value_fn_data) &updateStateCallBackTrampoline, this);
}

QString ProfileControl::profile()
{
    if (m_profile.isEmpty()) {
        m_profile = QString::fromUtf8(profile_get_profile());
    }

    return m_profile;
}

void ProfileControl::setProfile(const QString &profile)
{
    if (profile != m_profile) {
        m_profile = profile;
        profile_set_profile(profile.toUtf8().constData());
    }
}

int ProfileControl::ringerVolume() const
{
    return m_ringerVolume;
}

void ProfileControl::setRingerVolume(int volume)
{
    if (volume == m_ringerVolume) {
        return;
    }

    m_ringerVolume = volume;
    profile_set_value_as_int(GeneralProfile, VolumeKey, volume);
    emit ringerVolumeChanged();
}

int ProfileControl::vibraMode() const
{
    VibraMode result;

    if (m_vibraInGeneral) {
        if (m_vibraInSilent) {
            result = VibraAlways;
        } else {
            result = VibraNormal;
        }
    } else {
        if (m_vibraInSilent) {
            result = VibraSilent;
        } else {
            result = VibraNever;
        }
    }

    return result;
}

void ProfileControl::setVibraMode(int mode)
{
    bool generalValue = false;
    bool silentValue = false;

    switch (mode) {
    case VibraAlways:
        generalValue = true;
        silentValue = true;
        break;
    case VibraSilent:
        silentValue = true;
        break;
    case VibraNormal:
        generalValue = true;
        break;
    case VibraNever:
        break;
    }

    bool changed = false;
    if (generalValue != m_vibraInGeneral) {
        m_vibraInGeneral = generalValue;
        profile_set_value_as_bool(GeneralProfile, VibraKey, m_vibraInGeneral);
        changed = true;
    }
    if (silentValue != m_vibraInSilent) {
        m_vibraInSilent = silentValue;
        profile_set_value_as_bool(SilentProfile, VibraKey, m_vibraInSilent);
        changed = true;
    }

    if (changed) {
        emit vibraModeChanged();
    }
}

int ProfileControl::systemSoundLevel()
{
    if (m_systemSoundLevel == -1) {
        m_systemSoundLevel = profile_get_value_as_int(GeneralProfile, SystemSoundLevelKey);
    }
    return m_systemSoundLevel;
}

void ProfileControl::setSystemSoundLevel(int level)
{
    if (level == m_systemSoundLevel) {
        return;
    }
    m_systemSoundLevel = level;
    profile_set_value_as_int(GeneralProfile, SystemSoundLevelKey, level);
    emit systemSoundLevelChanged();
}

int ProfileControl::touchscreenToneLevel()
{
    if (m_touchscreenToneLevel == -1) {
        m_touchscreenToneLevel = profile_get_value_as_int(GeneralProfile, TouchscreenToneLevelKey);
    }
    return m_touchscreenToneLevel;
}

void ProfileControl::setTouchscreenToneLevel(int level)
{
    if (level == m_touchscreenToneLevel) {
        return;
    }
    m_touchscreenToneLevel = level;
    profile_set_value_as_int(GeneralProfile, TouchscreenToneLevelKey, level);
    emit touchscreenToneLevelChanged();
}

int ProfileControl::touchscreenVibrationLevel()
{
    if (m_touchscreenVibrationLevel == -1) {
        m_touchscreenVibrationLevel = profile_get_value_as_int(GeneralProfile, TouchscreenVibrationLevelKey);
    }
    return m_touchscreenVibrationLevel;
}

void ProfileControl::setTouchscreenVibrationLevel(int level)
{
    if (level == m_touchscreenVibrationLevel) {
        return;
    }
    m_touchscreenVibrationLevel = level;
    profile_set_value_as_int(GeneralProfile, TouchscreenVibrationLevelKey, level);
    emit touchscreenVibrationLevelChanged();
}

QString ProfileControl::ringerToneFile()
{
    if (m_ringerToneFile.isNull()) {
        m_ringerToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, RingerToneKey));
    }

    return m_ringerToneFile;
}

void ProfileControl::setRingerToneFile(const QString &filename)
{
    if (filename == m_ringerToneFile) {
        return;
    }

    m_ringerToneFile = filename;
    profile_set_value(GeneralProfile, RingerToneKey, filename.toUtf8().constData());
    emit ringerToneFileChanged();
}

QString ProfileControl::messageToneFile()
{
    if (m_messageToneFile.isNull()) {
        m_messageToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, MessageToneKey));
    }

    return m_messageToneFile;
}

void ProfileControl::setMessageToneFile(const QString &filename)
{
    if (filename == m_messageToneFile) {
        return;
    }

    m_messageToneFile = filename;
    profile_set_value(GeneralProfile, MessageToneKey, filename.toUtf8().constData());
    emit messageToneFileChanged();
}

QString ProfileControl::chatToneFile()
{
    if (m_chatToneFile.isNull()) {
        m_chatToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, ChatToneKey));
    }

    return m_chatToneFile;
}

void ProfileControl::setChatToneFile(const QString &filename)
{
    if (filename == m_chatToneFile) {
        return;
    }

    m_chatToneFile = filename;
    profile_set_value(GeneralProfile, ChatToneKey, filename.toUtf8().constData());
    emit chatToneFileChanged();
}

QString ProfileControl::mailToneFile()
{
    if (m_mailToneFile.isNull()) {
        m_mailToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, MailToneKey));
    }

    return m_mailToneFile;
}

void ProfileControl::setMailToneFile(const QString &filename)
{
    if (filename == m_mailToneFile) {
        return;
    }

    m_mailToneFile = filename;
    profile_set_value(GeneralProfile, MailToneKey, filename.toUtf8().constData());
    emit mailToneFileChanged();
}

QString ProfileControl::calendarToneFile()
{
    if (m_calendarToneFile.isNull()) {
        m_calendarToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, CalendarToneKey));
    }

    return m_calendarToneFile;
}

void ProfileControl::setCalendarToneFile(const QString &filename)
{
    if (filename == m_calendarToneFile) {
        return;
    }

    m_calendarToneFile = filename;
    profile_set_value(GeneralProfile, CalendarToneKey, filename.toUtf8().constData());
    emit calendarToneFileChanged();
}

QString ProfileControl::internetCallToneFile()
{
    if (m_internetCallToneFile.isNull()) {
        m_internetCallToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, InternetCallToneKey));
    }

    return m_internetCallToneFile;
}

void ProfileControl::setInternetCallToneFile(const QString &filename)
{
    if (filename == m_internetCallToneFile) {
        return;
    }

    m_internetCallToneFile = filename;
    profile_set_value(GeneralProfile, InternetCallToneKey, filename.toUtf8().constData());
    emit internetCallToneFileChanged();
}

QString ProfileControl::clockAlarmToneFile()
{
    if (m_clockAlarmToneFile.isNull()) {
        m_clockAlarmToneFile = QString::fromUtf8(profile_get_value(GeneralProfile, ClockAlarmToneKey));
    }

    return m_clockAlarmToneFile;
}

void ProfileControl::setClockAlarmToneFile(const QString &filename)
{
    if (filename == m_clockAlarmToneFile) {
        return;
    }

    m_clockAlarmToneFile = filename;
    profile_set_value(GeneralProfile, ClockAlarmToneKey, filename.toUtf8().constData());
    emit clockAlarmToneFileChanged();
}


bool ProfileControl::ringerToneEnabled()
{
    if (m_ringerToneEnabled == -1) {
        m_ringerToneEnabled = profile_get_value_as_bool(GeneralProfile, RingerToneEnabledKey);
    }
    return m_ringerToneEnabled;
}

void ProfileControl::setRingerToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_ringerToneEnabled) {
        return;
    }
    m_ringerToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, RingerToneEnabledKey, enabled);
    emit ringerToneEnabledChanged();
}

bool ProfileControl::messageToneEnabled()
{
    if (m_messageToneEnabled == -1) {
        m_messageToneEnabled = profile_get_value_as_bool(GeneralProfile, MessageToneEnabledKey);
    }
    return m_messageToneEnabled;
}

void ProfileControl::setMessageToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_messageToneEnabled) {
        return;
    }
    m_messageToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, MessageToneEnabledKey, enabled);
    emit messageToneEnabledChanged();
}

bool ProfileControl::chatToneEnabled()
{
    if (m_chatToneEnabled == -1) {
        m_chatToneEnabled = profile_get_value_as_bool(GeneralProfile, ChatToneEnabledKey);
    }
    return m_chatToneEnabled;
}

void ProfileControl::setChatToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_chatToneEnabled) {
        return;
    }
    m_chatToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, ChatToneEnabledKey, enabled);
    emit chatToneEnabledChanged();
}

bool ProfileControl::mailToneEnabled()
{
    if (m_mailToneEnabled == -1) {
        m_mailToneEnabled = profile_get_value_as_bool(GeneralProfile, MailToneEnabledKey);
    }
    return m_mailToneEnabled;
}

void ProfileControl::setMailToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_mailToneEnabled) {
        return;
    }
    m_mailToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, MailToneEnabledKey, enabled);
    emit mailToneEnabledChanged();
}

bool ProfileControl::internetCallToneEnabled()
{
    if (m_internetCallToneEnabled == -1) {
        m_internetCallToneEnabled = profile_get_value_as_bool(GeneralProfile, InternetCallToneEnabledKey);
    }
    return m_internetCallToneEnabled;
}

void ProfileControl::setInternetCallToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_internetCallToneEnabled) {
        return;
    }
    m_internetCallToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, InternetCallToneEnabledKey, enabled);
    emit internetCallToneEnabledChanged();
}

bool ProfileControl::calendarToneEnabled()
{
    if (m_calendarToneEnabled == -1) {
        m_calendarToneEnabled = profile_get_value_as_bool(GeneralProfile, CalendarToneEnabledKey);
    }
    return m_calendarToneEnabled;
}

void ProfileControl::setCalendarToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_calendarToneEnabled) {
        return;
    }
    m_calendarToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, CalendarToneEnabledKey, enabled);
    emit calendarToneEnabledChanged();
}

bool ProfileControl::clockAlarmToneEnabled()
{
    if (m_clockAlarmToneEnabled == -1) {
        m_clockAlarmToneEnabled = profile_get_value_as_bool(GeneralProfile, ClockAlarmToneEnabledKey);
    }
    return m_clockAlarmToneEnabled;
}

void ProfileControl::setClockAlarmToneEnabled(bool enabled)
{
    if (static_cast<int>(enabled) == m_clockAlarmToneEnabled) {
        return;
    }
    m_clockAlarmToneEnabled = enabled;
    profile_set_value_as_bool(GeneralProfile, ClockAlarmToneEnabledKey, enabled);
    emit clockAlarmToneEnabledChanged();
}


void ProfileControl::currentProfileChangedCallback(const char *name, ProfileControl *profileControl)
{
    QString newProfile = QString::fromUtf8(name);
    profileControl->m_profile = newProfile;
    emit profileControl->profileChanged(newProfile);
}

void ProfileControl::updateStateCallBack(const char *profile, const char *key, const char *val, const char *type)
{
    Q_UNUSED(type)

    if (qstrcmp(profile, GeneralProfile) == 0) {
        if (qstrcmp(key, VolumeKey) == 0) {
            int newVolume = QString(val).toInt();
            if (newVolume != m_ringerVolume) {
                m_ringerVolume = newVolume;
                emit ringerVolumeChanged();
            }
        } else if (qstrcmp(key, VibraKey) == 0) {
            bool newVibra = (qstrcmp(val, "On") == 0);
            if (newVibra != m_vibraInGeneral) {
                m_vibraInGeneral = newVibra;

                emit vibraModeChanged();
            }
        } else if (qstrcmp(key, SystemSoundLevelKey) == 0) {
            int newLevel = QString(val).toInt();
            if (newLevel != m_systemSoundLevel) {
                m_systemSoundLevel = newLevel;
                emit systemSoundLevelChanged();
            }
        } else if (qstrcmp(key, TouchscreenToneLevelKey) == 0) {
            int newLevel = QString(val).toInt();
            if (newLevel != m_touchscreenToneLevel) {
                m_touchscreenToneLevel = newLevel;
                emit touchscreenToneLevelChanged();
            }
        } else if (qstrcmp(key, TouchscreenVibrationLevelKey) == 0) {
            int newLevel = QString(val).toInt();
            if (newLevel != m_touchscreenVibrationLevel) {
                m_touchscreenVibrationLevel = newLevel;
                emit touchscreenVibrationLevelChanged();
            }

            // alarm files begin
        } else if (qstrcmp(key, RingerToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_ringerToneFile) {
                m_ringerToneFile = newFile;
                emit ringerToneFileChanged();
            }
        } else if (qstrcmp(key, MessageToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_messageToneFile) {
                m_messageToneFile = newFile;
                emit messageToneFileChanged();
            }
        } else if (qstrcmp(key, MailToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_mailToneFile) {
                m_mailToneFile = newFile;
                emit mailToneFileChanged();
            }
        } else if (qstrcmp(key, InternetCallToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_internetCallToneFile) {
                m_internetCallToneFile = newFile;
                emit internetCallToneFileChanged();
            }
        } else if (qstrcmp(key, CalendarToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_calendarToneFile) {
                m_calendarToneFile = newFile;
                emit calendarToneFileChanged();
            }
        } else if (qstrcmp(key, ClockAlarmToneKey) == 0) {
            QString newFile = val;
            if (newFile != m_clockAlarmToneFile) {
                m_clockAlarmToneFile = newFile;
                emit clockAlarmToneFileChanged();
            }

            // alarms enabled begin
        } else if (qstrcmp(key, RingerToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_ringerToneEnabled) {
                m_ringerToneEnabled = newEnabled;
                emit ringerToneEnabledChanged();
            }
        } else if (qstrcmp(key, MessageToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_messageToneEnabled) {
                m_messageToneEnabled = newEnabled;
                emit messageToneEnabledChanged();
            }
        } else if (qstrcmp(key, ChatToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_chatToneEnabled) {
                m_chatToneEnabled = newEnabled;
                emit chatToneEnabledChanged();
            }
        } else if (qstrcmp(key, MailToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_mailToneEnabled) {
                m_mailToneEnabled = newEnabled;
                emit mailToneEnabledChanged();
            }
        } else if (qstrcmp(key, InternetCallToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_internetCallToneEnabled) {
                m_internetCallToneEnabled = newEnabled;
                emit internetCallToneEnabledChanged();
            }
        } else if (qstrcmp(key, CalendarToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_calendarToneEnabled) {
                m_calendarToneEnabled = newEnabled;
                emit calendarToneEnabledChanged();
            }
        } else if (qstrcmp(key, ClockAlarmToneEnabledKey) == 0) {
            int newEnabled = profile_parse_bool(val);
            if (newEnabled != m_clockAlarmToneEnabled) {
                m_clockAlarmToneEnabled = newEnabled;
                emit clockAlarmToneEnabledChanged();
            }
        }

    } else if (qstrcmp(profile, SilentProfile) == 0) {
        if (qstrcmp(key, VibraKey) == 0) {
            bool newVibra = (qstrcmp(val, "On") == 0);
            if (newVibra != m_vibraInSilent) {
                m_vibraInGeneral = newVibra;

                emit vibraModeChanged();
            }
        }
    }
}

void ProfileControl::updateStateCallBackTrampoline(const char *profile, const char *key, const char *val,
                                                   const char *type, ProfileControl *profileControl)
{
    profileControl->updateStateCallBack(profile, key, val, type);
}
