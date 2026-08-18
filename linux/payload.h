#pragma once

#include <QFile>
#include <QTemporaryFile>

#include <functional>
#include <memory>

#include "update_metadata.pb.h"

struct PayloadPartitionRow {
    QString name;
    QString size;
    QString hash;
};

class PayloadReader {
public:
    using Progress = std::function<void(int, const QString &)>;

    explicit PayloadReader(QString sourcePath);
    ~PayloadReader();

    bool load(QString *error);
    bool isLoaded() const { return m_loaded; }

    const chromeos_update_engine::DeltaArchiveManifest &manifest() const { return m_manifest; }
    const QString &sourcePath() const { return m_sourcePath; }
    quint64 fileFormatVersion() const { return m_fileFormatVersion; }
    quint64 manifestSize() const { return m_manifestSize; }
    quint32 metadataSignatureSize() const { return m_metadataSignatureSize; }
    quint64 dataStart() const { return m_dataStart; }
    quint64 dataSize() const { return m_dataSize; }
    quint64 payloadSignatureSize() const { return m_payloadSignatureSize; }
    QString metadataSignature() const { return m_metadataSignature; }
    QString payloadSignature() const { return m_payloadSignature; }

    QList<PayloadPartitionRow> partitions() const;
    QStringList dynamicMetadata() const;
    QStringList imageInfo() const;

    bool extract(const QString &partitionName,
                 const QString &outputDirectory,
                 bool ignoreUnknown,
                 bool ignoreChecks,
                 QString *error,
                 const Progress &progress = {}) const;

private:
    bool openPayload(QString *error);
    bool readExact(qint64 size, QByteArray *result, QString *error) const;
    bool readAt(quint64 offset, quint64 size, QByteArray *result, QString *error) const;
    bool writeAt(QFile *file, quint64 offset, const QByteArray &data, QString *error) const;
    bool decompressBzip(const QByteArray &input, QByteArray *output, QString *error) const;
    bool decompressXz(const QByteArray &input, QByteArray *output, QString *error) const;
    bool verifyFile(const QString &path, const QByteArray &expected, QString *error) const;

    QString m_sourcePath;
    QString m_payloadPath;
    std::unique_ptr<QTemporaryFile> m_tempFile;
    mutable QFile m_file;
    bool m_loaded = false;

    quint64 m_fileFormatVersion = 0;
    quint64 m_manifestSize = 0;
    quint32 m_metadataSignatureSize = 0;
    quint64 m_dataStart = 0;
    quint64 m_dataSize = 0;
    quint64 m_payloadSignatureSize = 0;
    QString m_metadataSignature;
    QString m_payloadSignature;
    chromeos_update_engine::DeltaArchiveManifest m_manifest;
};

QString formatByteSize(quint64 bytes);
