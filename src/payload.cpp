#include "payload.h"
#include "platform_paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <bzlib.h>
#include <zip.h>
#include <lzma.h>

#include <algorithm>
#include <limits>

using chromeos_update_engine::DeltaArchiveManifest;
using chromeos_update_engine::DynamicPartitionGroup;
using chromeos_update_engine::InstallOperation;
using chromeos_update_engine::PartitionUpdate;
using chromeos_update_engine::Signatures;

namespace {

quint64 readBigEndian64(const QByteArray &bytes)
{
    quint64 result = 0;
    for (unsigned char byte : bytes)
        result = (result << 8) | byte;
    return result;
}

quint32 readBigEndian32(const QByteArray &bytes)
{
    quint32 result = 0;
    for (unsigned char byte : bytes)
        result = (result << 8) | byte;
    return result;
}

bool writeAll(QFile *file, const char *data, qint64 size, QString *error)
{
    qint64 written = 0;
    while (written < size) {
        const qint64 amount = file->write(data + written, size - written);
        if (amount <= 0) {
            if (error)
                *error = file->errorString();
            return false;
        }
        written += amount;
    }
    return true;
}

bool writeZeros(QFile *file, quint64 size, QString *error)
{
    static const QByteArray zeros(1024 * 1024, '\0');
    while (size > 0) {
        const qint64 amount = static_cast<qint64>(std::min<quint64>(size, zeros.size()));
        if (!writeAll(file, zeros.constData(), amount, error))
            return false;
        size -= static_cast<quint64>(amount);
    }
    return true;
}

QString base64(const std::string &value)
{
    return QString::fromLatin1(QByteArray::fromStdString(value).toBase64());
}

QString operationName(InstallOperation::Type type)
{
    switch (type) {
    case InstallOperation::REPLACE: return QStringLiteral("REPLACE");
    case InstallOperation::REPLACE_BZ: return QStringLiteral("REPLACE_BZ");
    case InstallOperation::MOVE: return QStringLiteral("MOVE");
    case InstallOperation::BSDIFF: return QStringLiteral("BSDIFF");
    case InstallOperation::SOURCE_COPY: return QStringLiteral("SOURCE_COPY");
    case InstallOperation::SOURCE_BSDIFF: return QStringLiteral("SOURCE_BSDIFF");
    case InstallOperation::DISCARD: return QStringLiteral("DISCARD");
    case InstallOperation::REPLACE_XZ: return QStringLiteral("REPLACE_XZ");
    case InstallOperation::PUFFDIFF: return QStringLiteral("PUFFDIFF");
    case InstallOperation::BROTLI_BSDIFF: return QStringLiteral("BROTLI_BSDIFF");
    case InstallOperation::ZUCCHINI: return QStringLiteral("ZUCCHINI");
    case InstallOperation::LZ4DIFF_BSDIFF: return QStringLiteral("LZ4DIFF_BSDIFF");
    case InstallOperation::LZ4DIFF_PUFFDIFF: return QStringLiteral("LZ4DIFF_PUFFDIFF");
    case InstallOperation::ZERO: return QStringLiteral("ZERO");
    default: return QString::number(static_cast<int>(type));
    }
}

struct ExtentRange {
    quint64 start = 0;
    quint64 length = 0;
};

bool collectExtentRanges(const InstallOperation &operation,
                         quint32 blockSize,
                         QList<ExtentRange> *ranges,
                         quint64 *total,
                         QString *error)
{
    if (blockSize == 0) {
        if (error) *error = QStringLiteral("Payload block 大小无效");
        return false;
    }

    ranges->clear();
    *total = 0;
    for (const auto &extent : operation.dst_extents()) {
        if (extent.start_block() > std::numeric_limits<quint64>::max() / blockSize
            || extent.num_blocks() > std::numeric_limits<quint64>::max() / blockSize) {
            if (error) *error = QStringLiteral("目标 extent 大小溢出");
            return false;
        }
        const quint64 start = extent.start_block() * blockSize;
        const quint64 length = extent.num_blocks() * blockSize;
        if (start > std::numeric_limits<quint64>::max() - length
            || *total > std::numeric_limits<quint64>::max() - length
            || start > static_cast<quint64>(std::numeric_limits<qint64>::max())
            || length > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
            if (error) *error = QStringLiteral("目标 extent 超出文件大小限制");
            return false;
        }
        ranges->append({start, length});
        *total += length;
    }
    return true;
}

bool isDataReplacement(InstallOperation::Type type)
{
    return type == InstallOperation::REPLACE
        || type == InstallOperation::REPLACE_BZ
        || type == InstallOperation::REPLACE_XZ;
}

bool isSupportedOperation(InstallOperation::Type type)
{
    return isDataReplacement(type)
        || type == InstallOperation::ZERO;
}

}

QString formatByteSize(quint64 bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QStringLiteral("%1 B").arg(bytes);
    return QStringLiteral("%1 %2").arg(value, 0, 'f', value >= 100.0 ? 0 : 2).arg(units[unit]);
}

PayloadReader::PayloadReader(QString sourcePath)
    : m_sourcePath(std::move(sourcePath))
{
}

PayloadReader::~PayloadReader()
{
    m_file.close();
}

bool PayloadReader::openPayload(QString *error)
{
    const QString lower = m_sourcePath.toLower();
    if (lower.endsWith(QStringLiteral(".zip"))) {
        int zipError = 0;
        zip_t *archive = zip_open(m_sourcePath.toUtf8().constData(), ZIP_RDONLY, &zipError);
        if (!archive) {
            if (error)
                *error = QStringLiteral("无法打开 ZIP 文件");
            return false;
        }

        const zip_int64_t index = zip_name_locate(archive, "payload.bin", 0);
        if (index < 0) {
            zip_close(archive);
            if (error)
                *error = QStringLiteral("ZIP 中没有 payload.bin");
            return false;
        }

        zip_file_t *entry = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
        if (!entry) {
            zip_close(archive);
            if (error)
                *error = QStringLiteral("无法读取 ZIP 中的 payload.bin");
            return false;
        }

        m_tempFile = std::make_unique<QTemporaryFile>(
            PlatformPaths::temporaryDirectoryPattern(QStringLiteral("payload")));
        m_tempFile->setAutoRemove(true);
        if (!m_tempFile->open()) {
            zip_fclose(entry);
            zip_close(archive);
            if (error)
                *error = QStringLiteral("无法创建临时文件：") + m_tempFile->errorString();
            return false;
        }

        QByteArray buffer(1024 * 1024, '\0');
        while (true) {
            const zip_int64_t count = zip_fread(entry, buffer.data(), static_cast<zip_uint64_t>(buffer.size()));
            if (count < 0) {
                zip_fclose(entry);
                zip_close(archive);
                if (error)
                    *error = QStringLiteral("读取 payload.bin 失败");
                return false;
            }
            if (count == 0)
                break;
            if (!writeAll(m_tempFile.get(), buffer.constData(), count, error)) {
                zip_fclose(entry);
                zip_close(archive);
                return false;
            }
        }
        m_tempFile->flush();
        m_tempFile->close();
        zip_fclose(entry);
        zip_close(archive);
        m_payloadPath = m_tempFile->fileName();
    } else {
        m_payloadPath = m_sourcePath;
    }

    m_file.setFileName(m_payloadPath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开 payload：") + m_file.errorString();
        return false;
    }
    return true;
}

bool PayloadReader::readExact(qint64 size, QByteArray *result, QString *error) const
{
    if (size < 0 || size > std::numeric_limits<int>::max()) {
        if (error)
            *error = QStringLiteral("payload 字段过大");
        return false;
    }
    *result = m_file.read(size);
    if (result->size() != size) {
        if (error)
            *error = QStringLiteral("payload 文件提前结束");
        return false;
    }
    return true;
}

bool PayloadReader::readAt(quint64 offset, quint64 size, QByteArray *result, QString *error) const
{
    if (offset > static_cast<quint64>(std::numeric_limits<qint64>::max())
        || size > static_cast<quint64>(std::numeric_limits<int>::max())) {
        if (error)
            *error = QStringLiteral("payload 数据块过大");
        return false;
    }
    if (!m_file.seek(static_cast<qint64>(offset))) {
        if (error)
            *error = m_file.errorString();
        return false;
    }
    return readExact(static_cast<qint64>(size), result, error);
}

bool PayloadReader::load(QString *error)
{
    if (m_loaded)
        return true;
    if (!openPayload(error))
        return false;

    QByteArray magic;
    if (!readExact(4, &magic, error) || magic != QByteArrayLiteral("CrAU")) {
        if (error && error->isEmpty())
            *error = QStringLiteral("payload magic 不匹配");
        return false;
    }

    QByteArray header;
    if (!readExact(8, &header, error))
        return false;
    m_fileFormatVersion = readBigEndian64(header);
    if (m_fileFormatVersion < 2) {
        if (error)
            *error = QStringLiteral("不支持 payload 格式版本 1");
        return false;
    }

    if (!readExact(8, &header, error))
        return false;
    m_manifestSize = readBigEndian64(header);
    if (!readExact(4, &header, error))
        return false;
    m_metadataSignatureSize = readBigEndian32(header);

    QByteArray manifestBytes;
    if (!readExact(static_cast<qint64>(m_manifestSize), &manifestBytes, error))
        return false;
    if (!m_manifest.ParseFromArray(manifestBytes.constData(), manifestBytes.size())) {
        if (error)
            *error = QStringLiteral("无法解析 payload manifest");
        return false;
    }

    QByteArray metadataBytes;
    if (!readExact(static_cast<qint64>(m_metadataSignatureSize), &metadataBytes, error))
        return false;
    Signatures metadata;
    if (!metadata.ParseFromArray(metadataBytes.constData(), metadataBytes.size())) {
        if (error)
            *error = QStringLiteral("无法解析 metadata 签名");
        return false;
    }
    if (metadata.signatures_size() > 0)
        m_metadataSignature = base64(metadata.signatures(0).data());

    m_dataStart = static_cast<quint64>(m_file.pos());
    const quint64 fileSize = static_cast<quint64>(m_file.size());
    if (m_dataStart > fileSize) {
        if (error) *error = QStringLiteral("payload 数据起始位置无效");
        return false;
    }
    if (m_manifest.has_signatures_offset()) {
        if (m_manifest.signatures_offset() > fileSize - m_dataStart) {
            if (error) *error = QStringLiteral("payload 签名位置超出文件范围");
            return false;
        }
        m_dataSize = m_manifest.signatures_offset();
    } else {
        m_dataSize = fileSize - m_dataStart;
    }
    if (m_manifest.has_signatures_size() && m_manifest.signatures_size() > 0) {
        if (!m_manifest.has_signatures_offset()
            || m_manifest.signatures_size() > fileSize - m_dataStart - m_manifest.signatures_offset()) {
            if (error) *error = QStringLiteral("payload 签名范围超出文件范围");
            return false;
        }
        QByteArray payloadSignatureBytes;
        if (!readAt(m_dataStart + m_manifest.signatures_offset(), m_manifest.signatures_size(),
                    &payloadSignatureBytes, error))
            return false;
        Signatures payloadSignatures;
        if (!payloadSignatures.ParseFromArray(payloadSignatureBytes.constData(), payloadSignatureBytes.size())) {
            if (error)
                *error = QStringLiteral("无法解析 payload 签名");
            return false;
        }
        m_payloadSignatureSize = m_manifest.signatures_size();
        if (payloadSignatures.signatures_size() > 0)
            m_payloadSignature = base64(payloadSignatures.signatures(0).data());
    }

    m_loaded = true;
    return true;
}

QList<PayloadPartitionRow> PayloadReader::partitions() const
{
    QList<PayloadPartitionRow> result;
    for (const PartitionUpdate &partition : m_manifest.partitions()) {
        PayloadPartitionRow row;
        row.name = QString::fromStdString(partition.partition_name());
        if (partition.has_new_partition_info() && partition.new_partition_info().has_size())
            row.size = formatByteSize(partition.new_partition_info().size());
        else
            row.size = QStringLiteral("未知");
        if (partition.has_new_partition_info() && partition.new_partition_info().has_hash())
            row.hash = base64(partition.new_partition_info().hash());
        else
            row.hash = QStringLiteral("未知");
        result.append(row);
    }
    return result;
}

QStringList PayloadReader::dynamicMetadata() const
{
    QStringList result;
    if (!m_manifest.has_dynamic_partition_metadata()) {
        result.append(QStringLiteral("没有动态分区元数据"));
        return result;
    }

    const auto &metadata = m_manifest.dynamic_partition_metadata();
    if (metadata.has_snapshot_enabled())
        result.append(QStringLiteral("Snapshot enabled: ") + (metadata.snapshot_enabled() ? QStringLiteral("是") : QStringLiteral("否")));
    if (metadata.has_vabc_enabled())
        result.append(QStringLiteral("VABC enabled: ") + (metadata.vabc_enabled() ? QStringLiteral("是") : QStringLiteral("否")));
    for (const DynamicPartitionGroup &group : metadata.groups()) {
        result.append(QStringLiteral("分区组：") + QString::fromStdString(group.name()));
        if (group.has_size())
            result.append(QStringLiteral("  大小：") + formatByteSize(group.size()));
        result.append(QStringLiteral("  包含分区："));
        for (const std::string &name : group.partition_names())
            result.append(QStringLiteral("    ") + QString::fromStdString(name));
    }
    return result;
}

QStringList PayloadReader::imageInfo() const
{
    QStringList result;
    if (!m_manifest.has_new_image_info())
        return result;
    const auto &info = m_manifest.new_image_info();
    if (info.has_board()) result.append(QStringLiteral("board: ") + QString::fromStdString(info.board()));
    if (info.has_key()) result.append(QStringLiteral("key: ") + QString::fromStdString(info.key()));
    if (info.has_channel()) result.append(QStringLiteral("channel: ") + QString::fromStdString(info.channel()));
    if (info.has_version()) result.append(QStringLiteral("version: ") + QString::fromStdString(info.version()));
    if (info.has_build_channel()) result.append(QStringLiteral("build_channel: ") + QString::fromStdString(info.build_channel()));
    if (info.has_build_version()) result.append(QStringLiteral("build_version: ") + QString::fromStdString(info.build_version()));
    return result;
}

QStringList PayloadReader::unsupportedOperations(const QString &partitionName) const
{
    QStringList result;
    for (const PartitionUpdate &partition : m_manifest.partitions()) {
        const QString name = QString::fromStdString(partition.partition_name());
        if (!partitionName.isEmpty() && name != partitionName)
            continue;
        for (const InstallOperation &operation : partition.operations()) {
            if (!isSupportedOperation(operation.type()))
                result.append(name + QStringLiteral(": ") + operationName(operation.type()));
        }
    }
    return result;
}

bool PayloadReader::decompressBzip(const QByteArray &input, QByteArray *output, QString *error) const
{
    if (static_cast<quint64>(input.size()) > std::numeric_limits<unsigned int>::max()) {
        if (error) *error = QStringLiteral("BZip2 数据块过大");
        return false;
    }

    bz_stream stream{};
    if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK) {
        if (error) *error = QStringLiteral("无法初始化 BZip2");
        return false;
    }
    stream.next_in = const_cast<char *>(input.constData());
    stream.avail_in = static_cast<unsigned int>(input.size());
    QByteArray buffer(1024 * 1024, '\0');
    int status = BZ_OK;
    while (status == BZ_OK) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<unsigned int>(buffer.size());
        status = BZ2_bzDecompress(&stream);
        const int produced = static_cast<int>(buffer.size() - stream.avail_out);
        if (produced > 0)
            output->append(buffer.constData(), produced);
    }
    BZ2_bzDecompressEnd(&stream);
    if (status != BZ_STREAM_END) {
        if (error) *error = QStringLiteral("BZip2 解压失败");
        return false;
    }
    return true;
}

bool PayloadReader::decompressXz(const QByteArray &input, QByteArray *output, QString *error) const
{
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_auto_decoder(&stream, UINT64_MAX, 0) != LZMA_OK) {
        if (error) *error = QStringLiteral("无法初始化 XZ");
        return false;
    }
    stream.next_in = reinterpret_cast<const uint8_t *>(input.constData());
    stream.avail_in = static_cast<size_t>(input.size());
    QByteArray buffer(1024 * 1024, '\0');
    lzma_ret status = LZMA_OK;
    while (status == LZMA_OK) {
        stream.next_out = reinterpret_cast<uint8_t *>(buffer.data());
        stream.avail_out = static_cast<size_t>(buffer.size());
        status = lzma_code(&stream, LZMA_FINISH);
        const int produced = static_cast<int>(buffer.size() - stream.avail_out);
        if (produced > 0)
            output->append(buffer.constData(), produced);
    }
    lzma_end(&stream);
    if (status != LZMA_STREAM_END) {
        if (error) *error = QStringLiteral("XZ 解压失败");
        return false;
    }
    return true;
}

bool PayloadReader::writeAt(QFile *file, quint64 offset, const QByteArray &data, QString *error) const
{
    if (offset > static_cast<quint64>(std::numeric_limits<qint64>::max())
        || !file->seek(static_cast<qint64>(offset))) {
        if (error) *error = file->errorString();
        return false;
    }
    return writeAll(file, data.constData(), data.size(), error);
}

bool PayloadReader::verifyFile(const QString &path, const QByteArray &expected, QString *error) const
{
    if (expected.isEmpty())
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
        hash.addData(file.read(1024 * 1024));
    if (hash.result() != expected) {
        if (error) *error = QStringLiteral("最终镜像 SHA-256 校验失败");
        return false;
    }
    return true;
}

bool PayloadReader::extract(const QString &partitionName,
                            const QString &outputDirectory,
                            bool ignoreUnknown,
                            bool ignoreChecks,
                            QString *error,
                            const Progress &progress) const
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9._+-]+$")).match(partitionName).hasMatch()) {
        if (error) *error = QStringLiteral("目标分区名包含非法字符");
        return false;
    }

    const PartitionUpdate *target = nullptr;
    for (const PartitionUpdate &partition : m_manifest.partitions()) {
        if (QString::fromStdString(partition.partition_name()) == partitionName) {
            target = &partition;
            break;
        }
    }
    if (!target) {
        if (error) *error = QStringLiteral("找不到目标分区");
        return false;
    }
    if (!QDir().mkpath(outputDirectory)) {
        if (error) *error = QStringLiteral("无法创建输出目录");
        return false;
    }

    const QString outputPath = QDir(outputDirectory).filePath(partitionName + QStringLiteral(".img"));
    QFile output(outputPath);
    if (!output.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        if (error) *error = output.errorString();
        return false;
    }

    quint64 expectedSize = 0;
    bool hasExpectedSize = false;
    if (target->has_new_partition_info() && target->new_partition_info().has_size()) {
        expectedSize = target->new_partition_info().size();
        hasExpectedSize = true;
        if (expectedSize > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
            if (error) *error = QStringLiteral("目标分区大小超出文件大小限制");
            return false;
        }
        if (!output.resize(static_cast<qint64>(expectedSize))) {
            if (error) *error = output.errorString();
            return false;
        }
    }

    const int total = std::max(1, target->operations_size());
    for (int index = 0; index < target->operations_size(); ++index) {
        const InstallOperation &operation = target->operations(index);
        if (progress)
            progress(index * 100 / total, QStringLiteral("%1: %2").arg(partitionName, operationName(operation.type())));
        if (operation.dst_extents_size() == 0) {
            if (error) *error = QStringLiteral("操作没有目标 extent");
            return false;
        }

        QList<ExtentRange> ranges;
        quint64 dstLength = 0;
        if (!collectExtentRanges(operation, m_manifest.block_size(), &ranges, &dstLength, error))
            return false;
        if (operation.has_dst_length() && operation.dst_length() != dstLength) {
            if (error) *error = QStringLiteral("目标 extent 总大小与操作声明不一致");
            return false;
        }
        if (hasExpectedSize) {
            for (const ExtentRange &range : ranges) {
                if (range.start > expectedSize || range.length > expectedSize - range.start) {
                    if (error) *error = QStringLiteral("目标 extent 超出分区大小");
                    return false;
                }
            }
        }

        if (!isSupportedOperation(operation.type())) {
            if (!ignoreUnknown) {
                if (error) *error = QStringLiteral("不支持的操作类型：") + operationName(operation.type());
                return false;
            }
            continue;
        }

        QByteArray data;
        if (isDataReplacement(operation.type())) {
            if (!operation.has_data_offset() || !operation.has_data_length()) {
                if (error) *error = QStringLiteral("数据替换操作缺少数据范围");
                return false;
            }
            if (operation.data_offset() > m_dataSize
                || operation.data_length() > m_dataSize - operation.data_offset()
                || m_dataStart > std::numeric_limits<quint64>::max() - operation.data_offset()) {
                if (error) *error = QStringLiteral("操作数据范围超出 payload 数据区");
                return false;
            }
            if (!readAt(m_dataStart + operation.data_offset(), operation.data_length(), &data, error))
                return false;

            if (!ignoreChecks && operation.has_data_sha256_hash()) {
                QCryptographicHash hash(QCryptographicHash::Sha256);
                hash.addData(data);
                if (hash.result() != QByteArray::fromStdString(operation.data_sha256_hash())) {
                    if (error) *error = QStringLiteral("操作数据 SHA-256 校验失败");
                    return false;
                }
            }

            QByteArray decoded;
            switch (operation.type()) {
            case InstallOperation::REPLACE:
                decoded = data;
                break;
            case InstallOperation::REPLACE_BZ:
                if (!decompressBzip(data, &decoded, error)) return false;
                break;
            case InstallOperation::REPLACE_XZ:
                if (!decompressXz(data, &decoded, error)) return false;
                break;
            default:
                if (error) *error = QStringLiteral("内部错误：未知的数据替换操作");
                return false;
            }

            if (!ignoreChecks && static_cast<quint64>(decoded.size()) != dstLength) {
                if (error) *error = QStringLiteral("解压后的数据大小与目标 extent 不一致");
                return false;
            }
            if (static_cast<quint64>(decoded.size()) > dstLength) {
                if (error) *error = QStringLiteral("镜像数据超过目标 extent");
                return false;
            }

            quint64 decodedOffset = 0;
            for (const ExtentRange &range : ranges) {
                if (decodedOffset >= static_cast<quint64>(decoded.size()))
                    break;
                const quint64 amount = std::min(range.length,
                                                static_cast<quint64>(decoded.size()) - decodedOffset);
                if (amount == 0)
                    continue;
                if (!writeAt(&output, range.start,
                             decoded.mid(static_cast<int>(decodedOffset), static_cast<int>(amount)), error))
                    return false;
                decodedOffset += amount;
            }
            continue;
        }

        if (operation.type() == InstallOperation::ZERO) {
            for (const ExtentRange &range : ranges) {
                if (!output.seek(static_cast<qint64>(range.start))
                    || !writeZeros(&output, range.length, error))
                    return false;
            }
        }
    }

    output.close();
    if (!ignoreChecks && target->has_new_partition_info()) {
        if (target->new_partition_info().has_size()) {
            if (target->new_partition_info().size() > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
                if (error) *error = QStringLiteral("最终镜像大小超出文件大小限制");
                return false;
            }
            const qint64 expectedSize = static_cast<qint64>(target->new_partition_info().size());
            if (QFileInfo(outputPath).size() != expectedSize) {
                if (error) *error = QStringLiteral("最终镜像大小校验失败");
                return false;
            }
        }
        if (target->new_partition_info().has_hash()
            && !verifyFile(outputPath, QByteArray::fromStdString(target->new_partition_info().hash()), error))
            return false;
    }
    if (progress)
        progress(100, QStringLiteral("%1: 完成").arg(partitionName));
    return true;
}
