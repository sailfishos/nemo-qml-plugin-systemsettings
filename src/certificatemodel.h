/*
 * Copyright (c) 2016 - 2019 Jolla Ltd.
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef CERTIFICATEMODEL_H
#define CERTIFICATEMODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QVariantMap>

#include "systemsettingsglobal.h"


struct X509Certificate;

class SYSTEMSETTINGS_EXPORT Certificate
{
public:
    Certificate(const X509Certificate &cert);

    QString commonName() const { return m_commonName; }
    QString countryName() const { return m_countryName; }
    QString organizationName() const { return m_organizationName; }
    QString organizationalUnitName() const { return m_organizationalUnitName; }
    QString primaryName() const { return m_primaryName; }
    QString secondaryName() const { return m_secondaryName; }

    QDateTime notValidBefore() const { return m_notValidBefore; }
    QDateTime notValidAfter() const { return m_notValidAfter; }

    QVariantMap details() const { return m_details; }

    QString issuerDisplayName() const { return m_issuerDisplayName; }

private:
    QString m_commonName;
    QString m_countryName;
    QString m_organizationName;
    QString m_organizationalUnitName;
    QString m_primaryName;
    QString m_secondaryName;

    QDateTime m_notValidBefore;
    QDateTime m_notValidAfter;

    QString m_issuerDisplayName;

    QVariantMap m_details;
};

class SYSTEMSETTINGS_EXPORT CertificateModel: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(BundleType bundleType READ bundleType WRITE setBundleType NOTIFY bundleTypeChanged)
    Q_PROPERTY(QString bundlePath READ bundlePath WRITE setBundlePath NOTIFY bundlePathChanged)
    Q_ENUMS(BundleType)

public:
    enum BundleType {
        NoBundle,
        TLSBundle,
        EmailBundle,
        ObjectSigningBundle,
        UserSpecifiedBundle,
    };

    enum CertificateRoles {
        CommonNameRole = Qt::UserRole + 1,
        CountryNameRole = Qt::UserRole + 2,
        OrganizationNameRole = Qt::UserRole + 3,
        OrganizationalUnitNameRole = Qt::UserRole + 4,
        PrimaryNameRole = Qt::UserRole + 5,
        SecondaryNameRole = Qt::UserRole + 6,
        NotValidBeforeRole = Qt::UserRole + 7,
        NotValidAfterRole = Qt::UserRole + 8,
        DetailsRole = Qt::UserRole + 9,
    };

    explicit CertificateModel(QObject *parent = 0);
    virtual ~CertificateModel();

    BundleType bundleType() const;
    void setBundleType(BundleType type);

    QString bundlePath() const;
    void setBundlePath(const QString &path);

    virtual int rowCount(const QModelIndex & parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex &index, int role) const;

    static QList<Certificate> getCertificates(const QString &bundlePath);
    static QList<Certificate> getCertificates(const QByteArray &pem);

Q_SIGNALS:
    void bundleTypeChanged();
    void bundlePathChanged();

protected:
    void refresh();

    QHash<int, QByteArray> roleNames() const;

private:
    BundleType m_type;
    QString m_path;
    QList<Certificate> m_certificates;
};

#endif
