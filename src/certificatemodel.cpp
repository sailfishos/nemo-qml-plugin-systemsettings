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

#include "certificatemodel.h"

#include <QFile>
#include <QRegularExpression>
#include <QDebug>
#include <functional>

#include <openssl/opensslv.h>
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace {

struct X509List;

}

struct X509Certificate
{
    QList<QPair<QString, QString>> subjectList(bool shortForm = false) const
    {
        return nameList(X509_get_subject_name(x509), shortForm);
    }
    QString subject(bool shortForm = true, const QString &separator = QString(", ")) const
    {
        return toString(subjectList(shortForm), separator);
    }
    QString subjectElement(int nid) const
    {
        return nameElement(X509_get_subject_name(x509), nid);
    }

    QList<QPair<QString, QString>> issuerList(bool shortForm = false) const
    {
        return nameList(X509_get_issuer_name(x509), shortForm);
    }
    QString issuer(bool shortForm = true, const QString &separator = QString(", ")) const
    {
        return toString(issuerList(shortForm), separator);
    }
    QString issuerElement(int nid) const
    {
        return nameElement(X509_get_issuer_name(x509), nid);
    }

    QString version() const
    {
        return QString::number(X509_get_version(x509) + 1);
    }

    QString serialNumber() const
    {
        return integerToString(X509_get_serialNumber(x509));
    }

    QDateTime notBefore() const
    {
        return toDateTime(X509_get_notBefore(x509));
    }

    QDateTime notAfter() const
    {
        return toDateTime(X509_get_notAfter(x509));
    }

    QList<QPair<QString, QString>> publicKeyList(bool shortForm = false) const
    {
        QList<QPair<QString, QString>> rv;

        if (EVP_PKEY *key = X509_get_pubkey(x509)) {
            rv.append(qMakePair(QStringLiteral("Algorithm"), idToString(EVP_PKEY_id(key), shortForm)));
            rv.append(qMakePair(QStringLiteral("Bits"), QString::number(EVP_PKEY_bits(key))));

            BIO *b = BIO_new(BIO_s_mem());
            EVP_PKEY_print_public(b, key, 0, 0);
            const QList<QPair<QString, QString>> &details(parseData(bioToString(b)));
            for (auto it = details.cbegin(), end = details.cend(); it != end; ++it) {
                rv.append(qMakePair(it->first, it->second));
            }
            BIO_free(b);

            EVP_PKEY_free(key);
        }

        return rv;
    }

    QList<QPair<QString, QString>> extensionList(bool shortForm = false) const
    {
        QList<QPair<QString, QString>> rv;

        for (int i = 0, n = sk_X509_EXTENSION_num(X509_get0_extensions(x509)); i < n; ++i) {
            X509_EXTENSION *extension = sk_X509_EXTENSION_value(X509_get0_extensions(x509), i);

            ASN1_OBJECT *object = X509_EXTENSION_get_object(extension);
            int nid = OBJ_obj2nid(object);
            if (nid == NID_undef)
                continue;

            QString name(objectToString(object, shortForm));
            if (X509_EXTENSION_get_critical(extension) > 0) {
                name.append(QStringLiteral(" (Critical)"));
            }

            BIO *b = BIO_new(BIO_s_mem());
            X509V3_EXT_print(b, extension, 0, 0);
            rv.append(qMakePair(name, bioToString(b)));
            BIO_free(b);
        }

        return rv;
    }

    QList<QPair<QString, QString>> signatureList(bool shortForm = false) const
    {
        QList<QPair<QString, QString>> rv;
        const X509_ALGOR *sig_alg;
        const ASN1_BIT_STRING *sig;
        X509_get0_signature(&sig,&sig_alg, x509);

        rv.append(qMakePair(QStringLiteral("Algorithm"), objectToString(sig_alg->algorithm, shortForm)));

        BIO *b = BIO_new(BIO_s_mem());
        X509_signature_dump(b, sig, 0);
        QString d(bioToString(b).replace(QChar('\n'), QString()));
        rv.append(qMakePair(QStringLiteral("Data"), d.trimmed()));
        BIO_free(b);

        return rv;
    }

private:
    static QString stringToString(ASN1_STRING *data)
    {
        return QString::fromUtf8(reinterpret_cast<const char*>(ASN1_STRING_get0_data((data))));
    }

    static QString timeToString(ASN1_TIME *data)
    {
        return stringToString(data);
    }

    static QString idToString(int nid, bool shortForm)
    {
        return QString::fromUtf8(shortForm ? OBJ_nid2sn(nid) : OBJ_nid2ln(nid));
    }

    static QString objectToString(ASN1_OBJECT *object, bool shortForm)
    {
        return idToString(OBJ_obj2nid(object), shortForm);
    }

    static QString integerToString(ASN1_INTEGER *integer)
    {
        if (integer->type != V_ASN1_INTEGER && integer->type != V_ASN1_NEG_INTEGER)
            return QString();

        quint64 value = 0;
        for (size_t i = 0, n = qMin(integer->length, 8); i < n; ++i)
            value = value << 8 | integer->data[i];

        QString rv = QString::number(value);
        if (integer->type == V_ASN1_NEG_INTEGER)
            rv.prepend(QStringLiteral("-"));
        return rv;
    }

    static QString bioToString(BIO *bio)
    {
        char *out = 0;
        int n = BIO_get_mem_data(bio, &out);
        return QString::fromUtf8(QByteArray::fromRawData(out, n));
    }

    static QList<QPair<QString, QString>> nameList(X509_NAME *name, bool shortForm = true)
    {
        QList<QPair<QString, QString>> rv;

        for (int i = 0, n = X509_NAME_entry_count(name); i < n; ++i) {
            X509_NAME_ENTRY *entry = X509_NAME_get_entry(name, i);
            ASN1_OBJECT *object = X509_NAME_ENTRY_get_object(entry);
            ASN1_STRING *data = X509_NAME_ENTRY_get_data(entry);
            rv.append(qMakePair(objectToString(object, shortForm), stringToString(data)));
        }

        return rv;
    }

    static QString nameElement(X509_NAME *name, int nid)
    {
        for (int i = 0, n = X509_NAME_entry_count(name); i < n; ++i) {
            X509_NAME_ENTRY *entry = X509_NAME_get_entry(name, i);
            ASN1_OBJECT *object = X509_NAME_ENTRY_get_object(entry);
            if (OBJ_obj2nid(object) == nid) {
                ASN1_STRING *data = X509_NAME_ENTRY_get_data(entry);
                return stringToString(data);
            }
        }

        return QString();
    }

    static QString toString(const QList<QPair<QString, QString>> &list, const QString &separator = QString(", "))
    {
        QString rv;

        for (auto it = list.cbegin(), end = list.cend(); it != end; ++it) {
            if (!rv.isEmpty()) {
                rv.append(separator);
            }
            rv.append(it->first);
            rv.append(QChar(':'));
            rv.append(it->second);
        }

        return rv;
    }

    static QDateTime toDateTime(ASN1_TIME *time)
    {
        const QString ts(timeToString(time));
        return (time->type == V_ASN1_GENERALIZEDTIME ? fromGENERALIZEDTIME(ts) : fromUTCTIME(ts));
    }

    static QDateTime fromUTCTIME(const QString &ts)
    {
        QDate d;
        QTime t;
        int offset = 0;

        // "YYMMDDhhmm[ss](Z|(+|-)hhmm)"
        const QRegularExpression re("([0-9]{2})([0-9]{2})([0-9]{2})([0-9]{2})([0-9]{2})([0-9]{2})?(Z)?(([+-])([0-9]{2})([0-9]{2}))?");
        QRegularExpressionMatch match = re.match(ts);
        if (match.hasMatch()) {
            int y = match.captured(1).toInt();
            d = QDate((y < 70 ? 2000 : 1900) + y, match.captured(2).toInt(), match.captured(3).toInt());

            t = QTime(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt());

            if (match.lastCapturedIndex() > 7) {
                offset = match.captured(11).toInt() * 60 + match.captured(10).toInt() * 60*60;
                if (match.captured(9) == "-") {
                    offset = -offset;
                }
            }
        }

        return QDateTime(d, t, Qt::OffsetFromUTC, offset);
    }

    static QDateTime fromGENERALIZEDTIME(const QString &ts)
    {
        QDate d;
        QTime t;
        int offset = 0;

        // "YYYYMMDDhh[mm[ss[.fff]]](Z|(+|-)hhmm)" <- nested optionals can be treated as appearing sequentially
        const QRegularExpression re("([0-9]{4})([0-9]{2})([0-9]{2})([0-9]{2})([0-9]{2})?([0-9]{2})?(\\.[0-9]{1,3})?(Z)?(([+-])([0-9]{2})([0-9]{2}))?");
        QRegularExpressionMatch match = re.match(ts);
        if (match.hasMatch()) {
            d = QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());

            double fraction = match.captured(7).toDouble();
            int ms = (fraction * 1000);
            t = QTime(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt(), ms);

            if (match.lastCapturedIndex() > 8) {
                offset = match.captured(12).toInt() * 60 + match.captured(11).toInt() * 60*60;
                if (match.captured(10) == "-") {
                    offset = -offset;
                }
            }
        }

        return QDateTime(d, t, Qt::OffsetFromUTC, offset);
    }

    static QList<QPair<QString, QString>> parseData(QString data)
    {
        QList<QPair<QString, QString>> rv;

        // Join any data with the preceding header
        data.replace(QRegularExpression(": *\n +"), QStringLiteral(":"));
        foreach (const QString &line, data.split(QString("\n"), QString::SkipEmptyParts)) {
            int index = line.indexOf(QChar(':'));
            if (index != -1) {
                QString name(line.left(index));
                QString value(line.mid(index + 1).trimmed());
                rv.append(qMakePair(name, value));
            }
        }

        return rv;
    }

    friend struct ::X509List;

    X509Certificate(X509 *x) : x509(x) {}

    X509 *x509 = X509_new();
};

namespace {

struct X509List
{
    X509List()
        : crlStack(0), certificateStack(0), pkcs7(0), pkcs7Signed(0)
    {
        crlStack = sk_X509_CRL_new_null();
        if (!crlStack) {
            qWarning() << "Unable to allocate CRL stack";
        } else {
            certificateStack = sk_X509_new_null();
            if (!certificateStack) {
                qWarning() << "Unable to allocate X509 stack";
            } else {
                pkcs7 = PKCS7_new();
                pkcs7Signed = PKCS7_SIGNED_new();
                if (!pkcs7 || !pkcs7Signed) {
                    qWarning() << "Unable to create PKCS7 structures";
                } else {
                    pkcs7Signed->crl = crlStack;
                    pkcs7Signed->cert = certificateStack;

                    pkcs7->type = OBJ_nid2obj(NID_pkcs7_signed);
                    pkcs7->d.sign = pkcs7Signed;
                    pkcs7Signed->contents->type = OBJ_nid2obj(NID_pkcs7_data);
                    if (!ASN1_INTEGER_set(pkcs7Signed->version, 1)) {
                        qWarning() << "Unable to set PKCS7 signed version";
                    }
                }
            }
        }
    }

    ~X509List()
    {
        /* Apparently, pkcs7Signed and pkcs7 cannot be safely freed...
        if (pkcs7Signed)
            PKCS7_SIGNED_free(pkcs7Signed);
        if (pkcs7)
            PKCS7_free(pkcs7);
        */
        if (certificateStack)
            sk_X509_free(certificateStack);
        if (crlStack)
            sk_X509_CRL_free(crlStack);
    }

    bool isValid() const
    {
        return pkcs7 && pkcs7Signed;
    }

    int count() const
    {
        return sk_X509_num(certificateStack);
    }

    void append(X509 *x509)
    {
        sk_X509_push(certificateStack, x509);
    }

    void for_each(std::function<void (const X509Certificate &)> fn) const
    {
        for (int i = 0, n(count()); i < n; ++i) {
            fn(X509Certificate(sk_X509_value(certificateStack, i)));
        }
    }

private:
    STACK_OF(X509_CRL) *crlStack;
    STACK_OF(X509) *certificateStack;
    PKCS7 *pkcs7;
    PKCS7_SIGNED *pkcs7Signed;
};

struct PKCS7File
{
    explicit PKCS7File(const QString &path)
    {
        if (!isValid()) {
            qWarning() << "Unable to prepare X509 certificates structure";
        } else {
            BIO *input = BIO_new(BIO_s_file());
            if (!input) {
                qWarning() << "Unable to allocate new BIO for:" << path;
            } else {
                const QByteArray filename(QFile::encodeName(path));
                if (BIO_read_filename(input, const_cast<char *>(filename.constData())) <= 0) {
                    qWarning() << "Unable to open PKCS7 file:" << path;
                } else {
                    read_pem_from_bio(input);
                }

                BIO_free(input);
            }
        }
    }

    explicit PKCS7File(const QByteArray &pem)
    {
        if (!isValid()) {
            qWarning() << "Unable to prepare X509 certificates structure";
        } else {
            BIO *input = BIO_new_mem_buf(pem.constData(), pem.length());
            if (!input) {
                qWarning() << "Unable to allocate new BIO while importing in-memory PEM";
            } else {
                read_pem_from_bio(input);
                BIO_free(input);
            }
        }
    }

    void read_pem_from_bio(BIO *input) {
        STACK_OF(X509_INFO) *certificateStack = PEM_X509_INFO_read_bio(input, NULL, NULL, NULL);
        if (!certificateStack) {
            qWarning() << "Unable to read PKCS7 data";
        } else {
            while (sk_X509_INFO_num(certificateStack)) {
                X509_INFO *certificateInfo = sk_X509_INFO_shift(certificateStack);
                if (certificateInfo->x509 != NULL) {
                    certs.append(certificateInfo->x509);
                    certificateInfo->x509 = NULL;
                }
                X509_INFO_free(certificateInfo);
            }

            sk_X509_INFO_free(certificateStack);
        }
    }

    ~PKCS7File()
    {
    }

    bool isValid() const
    {
        return certs.isValid();
    }

    int count() const
    {
        return certs.count();
    }

    const X509List &getCertificates()
    {
        return certs;
    }

private:
    X509List certs;
};

class LibCrypto
{
    struct Initializer
    {
        Initializer()
        {
        }

        ~Initializer()
        {
        }
    };

    static Initializer init;

public:
    template<class T>
    static QList<Certificate> getCertificates(const T &bundleData)
    {
        PKCS7File bundle(bundleData);

        return bundleToCertificates(bundle);
    }
private:
    static QList<Certificate> bundleToCertificates(PKCS7File &bundle)
    {
        QList<Certificate> certificates;
        if (bundle.isValid() && bundle.count() > 0) {
            certificates.reserve(bundle.count());
            bundle.getCertificates().for_each([&certificates](const X509Certificate &cert) {
                certificates.append(Certificate(cert));
            });
        }

        return certificates;
    }
};


LibCrypto::Initializer LibCrypto::init;

const QList<QPair<QString, CertificateModel::BundleType> > &bundlePaths()
{
    static QList<QPair<QString, CertificateModel::BundleType> > paths;
    if (paths.isEmpty()) {
        paths.append(qMakePair(QString("/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"), CertificateModel::TLSBundle));
        paths.append(qMakePair(QString("/etc/pki/ca-trust/extracted/pem/email-ca-bundle.pem"), CertificateModel::EmailBundle));
        paths.append(qMakePair(QString("/etc/pki/ca-trust/extracted/pem/objsign-ca-bundle.pem"), CertificateModel::ObjectSigningBundle));
    }
    return paths;
}

CertificateModel::BundleType bundleType(const QString &path)
{
    if (path.isEmpty())
        return CertificateModel::NoBundle;

    const QList<QPair<QString, CertificateModel::BundleType> > &bundles(bundlePaths());
    for (auto it = bundles.cbegin(), end = bundles.cend(); it != end; ++it) {
        if (it->first == path)
            return it->second;
    }

    return CertificateModel::UserSpecifiedBundle;
}

QString bundlePath(CertificateModel::BundleType type)
{
    if (type == CertificateModel::UserSpecifiedBundle)
        return QString();

    const QList<QPair<QString, CertificateModel::BundleType> > &bundles(bundlePaths());
    for (auto it = bundles.cbegin(), end = bundles.cend(); it != end; ++it) {
        if (it->second == type)
            return it->first;
    }

    return QStringLiteral("");
}

}

Certificate::Certificate(const X509Certificate &cert)
    : m_commonName(cert.subjectElement(NID_commonName))
    , m_countryName(cert.subjectElement(NID_countryName))
    , m_organizationName(cert.subjectElement(NID_organizationName))
    , m_organizationalUnitName(cert.subjectElement(NID_organizationalUnitName))
    , m_notValidBefore(cert.notBefore())
    , m_notValidAfter(cert.notAfter())
{
    // Yield consistent names for the certificates, despite inconsistent naming policy
    QString Certificate::*members[] = { &Certificate::m_commonName, &Certificate::m_organizationalUnitName, &Certificate::m_organizationName, &Certificate::m_countryName };
    for (auto it = std::begin(members); it != std::end(members); ++it) {
        const QString &s(this->*(*it));
        if (!s.isEmpty()) {
            if (m_primaryName.isEmpty()) {
                m_primaryName = s;
            } else if (m_secondaryName.isEmpty()) {
                m_secondaryName = s;
                break;
            }
        }
    }

    // Matches QSslCertificate::issuerDisplayName() introducd in Qt 5.12
    // Returns a name that describes the issuer. It returns the CommonName if
    // available, otherwise falls back to the Organization or the first
    // OrganizationalUnitName.
    m_issuerDisplayName = cert.issuerElement(NID_commonName);
    if (m_issuerDisplayName.isEmpty()) {
        m_issuerDisplayName = cert.issuerElement(NID_countryName);
    }
    if (m_issuerDisplayName.isEmpty()) {
        m_issuerDisplayName = cert.issuerElement(NID_organizationName);
    }

    // Populate the details map
    m_details.insert(QStringLiteral("Version"), QVariant(cert.version()));
    m_details.insert(QStringLiteral("SerialNumber"), QVariant(cert.serialNumber()));
    m_details.insert(QStringLiteral("SubjectDisplayName"), QVariant(m_primaryName));
    m_details.insert(QStringLiteral("OrganizationName"), QVariant(m_organizationName));
    m_details.insert(QStringLiteral("IssuerDisplayName"), QVariant(m_issuerDisplayName));

    QVariantMap validity;
    validity.insert(QStringLiteral("NotBefore"), QVariant(cert.notBefore()));
    validity.insert(QStringLiteral("NotAfter"), QVariant(cert.notAfter()));
    m_details.insert(QStringLiteral("Validity"), QVariant(validity));

    QVariantMap issuer;
    const QList<QPair<QString, QString>> &issuerDetails(cert.issuerList());
    for (auto it = issuerDetails.cbegin(), end = issuerDetails.cend(); it != end; ++it) {
        issuer.insert(it->first, QVariant(it->second));
    }
    m_details.insert(QStringLiteral("Issuer"), QVariant(issuer));

    QVariantMap subject;
    const QList<QPair<QString, QString>> &subjectDetails(cert.subjectList());
    for (auto it = subjectDetails.cbegin(), end = subjectDetails.cend(); it != end; ++it) {
        subject.insert(it->first, QVariant(it->second));
    }
    m_details.insert(QStringLiteral("Subject"), QVariant(subject));

    QVariantMap publicKey;
    const QList<QPair<QString, QString>> &keyDetails(cert.publicKeyList());
    for (auto it = keyDetails.cbegin(), end = keyDetails.cend(); it != end; ++it) {
        // publicKeyList() adds "Bits" and "Algorithm" fields which include the same thing more nicely
        if (it->first == QLatin1String("Public-Key") || it->first == QLatin1String("RSA Public-Key")) {
            continue;
        }
        publicKey.insert(it->first, QVariant(it->second));
    }
    m_details.insert(QStringLiteral("SubjectPublicKeyInfo"), QVariant(publicKey));

    QVariantMap extensions;
    const QList<QPair<QString, QString>> &extensionDetails(cert.extensionList());
    for (auto it = extensionDetails.cbegin(), end = extensionDetails.cend(); it != end; ++it) {
        extensions.insert(it->first, QVariant(it->second));
    }
    m_details.insert(QStringLiteral("Extensions"), extensions);

    QVariantMap signature;
    const QList<QPair<QString, QString>> &signatureDetails(cert.signatureList());
    for (auto it = signatureDetails.cbegin(), end = signatureDetails.cend(); it != end; ++it) {
        signature.insert(it->first, QVariant(it->second));
    }
    m_details.insert(QStringLiteral("Signature"), signature);
}

CertificateModel::CertificateModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_type(NoBundle)
{
}

CertificateModel::~CertificateModel()
{
}

CertificateModel::BundleType CertificateModel::bundleType() const
{
    return m_type;
}

void CertificateModel::setBundleType(BundleType type)
{
    if (m_type != type) {
        m_type = type;

        const QString path(::bundlePath(m_type));
        if (!path.isNull())
            setBundlePath(path);

        emit bundleTypeChanged();
    }
}

QString CertificateModel::bundlePath() const
{
    return m_path;
}

void CertificateModel::setBundlePath(const QString &path)
{
    if (m_path != path) {
        m_path = path;
        refresh();

        const BundleType type(::bundleType(m_path));
        setBundleType(type);

        emit bundlePathChanged();
    }
}

int CertificateModel::rowCount(const QModelIndex & parent) const
{
    Q_UNUSED(parent)
    return m_certificates.count();
}

QVariant CertificateModel::data(const QModelIndex &index, int role) const
{
    int row = index.row();
    if (row < 0 || row >= m_certificates.count()) {
        return QVariant();
    }

    const Certificate &cert = m_certificates.at(row);
    switch (role) {
    case CommonNameRole:
        return cert.commonName();
    case CountryNameRole:
        return cert.countryName();
    case OrganizationNameRole:
        return cert.organizationName();
    case OrganizationalUnitNameRole:
        return cert.organizationalUnitName();
    case PrimaryNameRole:
        return cert.primaryName();
    case SecondaryNameRole:
        return cert.secondaryName();
    case NotValidBeforeRole:
        return cert.notValidBefore();
    case NotValidAfterRole:
        return cert.notValidAfter();
    case DetailsRole:
        return cert.details();
    default:
        break;
    }

    return QVariant();
}

QHash<int, QByteArray> CertificateModel::roleNames() const
{
    QHash<int, QByteArray> roles;

    roles[CommonNameRole] = "commonName";
    roles[CountryNameRole] = "countryName";
    roles[OrganizationNameRole] = "organizationName";
    roles[OrganizationalUnitNameRole] = "organizationalUnitName";
    roles[PrimaryNameRole] = "primaryName";
    roles[SecondaryNameRole] = "secondaryName";
    roles[NotValidBeforeRole] = "notValidBefore";
    roles[NotValidAfterRole] = "notValidAfter";
    roles[DetailsRole] = "details";

    return roles;
}
void CertificateModel::refresh()
{
    beginResetModel();
    if (m_path.isEmpty()) {
        m_certificates.clear();
    } else {
        m_certificates = getCertificates(m_path);
        std::stable_sort(m_certificates.begin(), m_certificates.end(), [](const Certificate &lhs, const Certificate &rhs) {
            int c = lhs.primaryName().compare(rhs.primaryName(), Qt::CaseInsensitive);
            if (c < 0)
                return true;
            if (c > 0)
                return false;
            c = lhs.secondaryName().compare(rhs.secondaryName(), Qt::CaseInsensitive);
            if (c < 0)
                return true;
            return false;
        });
    }
    endResetModel();
}

QList<Certificate> CertificateModel::getCertificates(const QString &bundlePath)
{
    return LibCrypto::getCertificates(bundlePath);
}

QList<Certificate> CertificateModel::getCertificates(const QByteArray &pem)
{
    return LibCrypto::getCertificates(pem);
}
