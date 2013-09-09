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

#ifndef LANGUAGEMODEL_H
#define LANGUAGEMODEL_H

#include <QAbstractListModel>
#include <QList>

class Language {
public:
    Language(QString name, QString localeCode, QString region, QString regionLabel);
    QString name() const;
    QString localeCode() const;
    QString region() const;
    QString regionLabel() const;

private:
    QString m_name;
    QString m_localeCode;
    QString m_region;
    QString m_regionLabel;
};

class LanguageModel: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_ENUMS(LocaleUpdateMode)

public:
    enum LanguageRoles {
        NameRole = Qt::UserRole + 1,
        LocaleRole,
        RegionRole,
        RegionLabelRole
    };

    enum LocaleUpdateMode {
        UpdateAndReboot,
        UpdateWithoutReboot
    };

    explicit LanguageModel(QObject *parent = 0);
    virtual ~LanguageModel();

    virtual int rowCount(const QModelIndex & parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role) const;

    int currentIndex() const;

    Q_INVOKABLE QString languageName(int index) const;
    Q_INVOKABLE QString locale(int index) const;

    Q_INVOKABLE void setSystemLocale(const QString &localeCode, LocaleUpdateMode updateMode);

    static QList<Language> supportedLanguages();

signals:
    void currentIndexChanged();

protected:
    QHash<int, QByteArray> roleNames() const;

private:
    void readCurrentLocale();
    int getLocaleIndex(const QString &locale) const;

    QList<Language> m_languages;
    int m_currentIndex;
};

#endif
