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

#include "alarmtonemodel.h"

#include <QDir>
#include <QDebug>
#include <QQmlEngine>
#include <qqml.h>

const char * const AlarmToneDir = "/usr/share/sounds/jolla-ringtones/stereo/";


AlarmToneModel::AlarmToneModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QDir ringtoneDir(AlarmToneDir);
    QStringList filters;
    filters << "*.wav" << "*.mp3" << "*.ogg"; // TODO: need more?
    m_fileInfoList = ringtoneDir.entryInfoList(filters, QDir::Files, QDir::Name);
}

AlarmToneModel::~AlarmToneModel()
{
}

QHash<int, QByteArray> AlarmToneModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[FilenameRole] = "filename";
    roles[TitleRole] = "title";

    return roles;
}

int AlarmToneModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_fileInfoList.count();
}

QVariant AlarmToneModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if (row < 0 || row > m_fileInfoList.count()) {
        return QVariant();
    }

    switch (role) {
    case FilenameRole:
        return m_fileInfoList.at(row).absoluteFilePath();
    case TitleRole:
        // for now just strip extension
        return m_fileInfoList.at(row).baseName();
    default:
        return QVariant();
    }
}

QJSValue AlarmToneModel::get(int index) const
{
    if (index < 0 || m_fileInfoList.count() <= index) {
        return QJSValue();
    }

    QFileInfo info = m_fileInfoList.at(index);
    QJSEngine *const engine = qmlEngine(this);
    QJSValue value = engine->newObject();

    value.setProperty("filename", engine->toScriptValue(info.absoluteFilePath()));
    value.setProperty("title",   engine->toScriptValue(info.baseName()));

    return value;
}
