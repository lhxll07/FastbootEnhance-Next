#include "platform_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString executableName(const QString &name)
{
#if defined(Q_OS_WIN)
    if (QFileInfo(name).suffix().isEmpty())
        return name + QStringLiteral(".exe");
#endif
    return name;
}

bool usableTool(const QFileInfo &info)
{
#if defined(Q_OS_WIN)
    return info.isFile();
#else
    return info.isFile() && info.isExecutable();
#endif
}

void appendCandidate(QStringList *candidates, const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (!candidates->contains(clean))
        candidates->append(clean);
}

QString overrideVariable(const QString &name)
{
    QString variable = QStringLiteral("FASTBOOT_ENHANCE_") + name.toUpper();
    const QByteArray key = variable.toLocal8Bit();
    return qEnvironmentVariable(key.constData());
}

QStringList sdkRoots()
{
    const QString home = QDir::homePath();
    QStringList roots;
#if defined(Q_OS_WIN)
    roots << QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"))
          + QStringLiteral("/Android/Sdk/platform-tools")
          << home + QStringLiteral("/AppData/Local/Android/Sdk/platform-tools");
#elif defined(Q_OS_MACOS)
    roots << home + QStringLiteral("/Library/Android/sdk/platform-tools");
#else
    roots << home + QStringLiteral("/Android/Sdk/platform-tools");
#endif
    return roots;
}

}

namespace PlatformPaths {

QString resolveTool(const QString &name)
{
    const QString executable = executableName(name);
    const QString overridePath = overrideVariable(name);
    if (!overridePath.isEmpty()) {
        const QFileInfo info(overridePath);
        if (usableTool(info))
            return info.absoluteFilePath();
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
    appendCandidate(&candidates, appDir + QStringLiteral("/bin/") + executable);
    appendCandidate(&candidates, appDir + QStringLiteral("/tools/") + executable);
    appendCandidate(&candidates, appDir + QStringLiteral("/") + executable);
#if defined(Q_OS_MACOS)
    appendCandidate(&candidates, appDir + QStringLiteral("/../Resources/bin/") + executable);
    appendCandidate(&candidates, appDir + QStringLiteral("/../Resources/tools/") + executable);
#endif
    for (const QString &root : sdkRoots())
        appendCandidate(&candidates, root + QStringLiteral("/") + executable);

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (usableTool(info))
            return info.absoluteFilePath();
    }

    const QString path = QStandardPaths::findExecutable(executable);
    return path.isEmpty() ? executable : path;
}

QString temporaryDirectoryPattern(const QString &purpose)
{
    QString safePurpose = purpose;
    safePurpose.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("-"));
    return QDir::tempPath() + QStringLiteral("/FastbootEnhance-") + safePurpose + QStringLiteral("-XXXXXX");
}

QString platformName()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Linux");
#else
    return QStringLiteral("未知平台");
#endif
}

}
