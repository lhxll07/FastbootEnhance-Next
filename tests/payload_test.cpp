#include "payload.h"

#include "update_metadata.pb.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdint>

using chromeos_update_engine::DeltaArchiveManifest;
using chromeos_update_engine::InstallOperation;
using chromeos_update_engine::Signatures;

namespace {

void appendBigEndian(QByteArray *target, std::uint64_t value, int bytes)
{
    for (int index = bytes - 1; index >= 0; --index)
        target->append(static_cast<char>((value >> (index * 8)) & 0xff));
}

bool fail(const QString &message)
{
    qCritical("%s", qPrintable(message));
    return false;
}

bool runTest()
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return fail(QStringLiteral("无法创建测试临时目录"));

    const QByteArray replacement = QByteArrayLiteral("ABCDEFGH");
    const QByteArray expected = QByteArrayLiteral("ABCD\0\0\0\0EFGH");

    DeltaArchiveManifest manifest;
    manifest.set_block_size(4);
    manifest.set_minor_version(0);
    manifest.set_signatures_offset(replacement.size());

    auto *partition = manifest.add_partitions();
    partition->set_partition_name("system");
    auto *partitionInfo = partition->mutable_new_partition_info();
    partitionInfo->set_size(expected.size());
    partitionInfo->set_hash(QCryptographicHash::hash(expected, QCryptographicHash::Sha256).toStdString());

    auto *replace = partition->add_operations();
    replace->set_type(InstallOperation::REPLACE);
    replace->set_data_offset(0);
    replace->set_data_length(replacement.size());
    replace->set_dst_length(replacement.size());
    auto *firstExtent = replace->add_dst_extents();
    firstExtent->set_start_block(0);
    firstExtent->set_num_blocks(1);
    auto *secondExtent = replace->add_dst_extents();
    secondExtent->set_start_block(2);
    secondExtent->set_num_blocks(1);

    auto *zero = partition->add_operations();
    zero->set_type(InstallOperation::ZERO);
    zero->set_dst_length(4);
    auto *zeroExtent = zero->add_dst_extents();
    zeroExtent->set_start_block(1);
    zeroExtent->set_num_blocks(1);

    Signatures metadata;
    metadata.add_signatures()->set_data("metadata");
    std::string metadataBytes;
    if (!metadata.SerializeToString(&metadataBytes))
        return fail(QStringLiteral("无法序列化 metadata 签名"));

    Signatures payloadSignatures;
    payloadSignatures.add_signatures()->set_data("payload");
    std::string payloadSignatureBytes;
    if (!payloadSignatures.SerializeToString(&payloadSignatureBytes))
        return fail(QStringLiteral("无法序列化 payload 签名"));
    manifest.set_signatures_size(payloadSignatureBytes.size());

    std::string manifestBytes;
    if (!manifest.SerializeToString(&manifestBytes))
        return fail(QStringLiteral("无法序列化 manifest"));

    QByteArray payload = QByteArrayLiteral("CrAU");
    appendBigEndian(&payload, 2, 8);
    appendBigEndian(&payload, manifestBytes.size(), 8);
    appendBigEndian(&payload, metadataBytes.size(), 4);
    payload.append(QByteArray::fromStdString(manifestBytes));
    payload.append(QByteArray::fromStdString(metadataBytes));
    payload.append(replacement);
    payload.append(QByteArray::fromStdString(payloadSignatureBytes));

    const QString payloadPath = temporary.filePath(QStringLiteral("payload.bin"));
    QFile payloadFile(payloadPath);
    if (!payloadFile.open(QIODevice::WriteOnly) || payloadFile.write(payload) != payload.size())
        return fail(QStringLiteral("无法写入测试 Payload"));
    payloadFile.close();

    PayloadReader reader(payloadPath);
    QString error;
    if (!reader.load(&error))
        return fail(QStringLiteral("读取测试 Payload 失败：") + error);
    if (reader.partitions().size() != 1 || reader.partitions().first().name != QStringLiteral("system"))
        return fail(QStringLiteral("Payload 分区信息不正确"));

    const QString outputDirectory = temporary.filePath(QStringLiteral("output"));
    if (!reader.extract(QStringLiteral("system"), outputDirectory, false, false, &error))
        return fail(QStringLiteral("提取测试 Payload 失败：") + error);

    QFile output(QDir(outputDirectory).filePath(QStringLiteral("system.img")));
    if (!output.open(QIODevice::ReadOnly) || output.readAll() != expected)
        return fail(QStringLiteral("提取结果与预期不一致"));
    if (QFileInfo(output).size() != expected.size())
        return fail(QStringLiteral("提取结果大小不正确"));
    return true;
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    return runTest() ? 0 : 1;
}
