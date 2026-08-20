#pragma once

#include <QString>

namespace PlatformPaths {

QString resolveTool(const QString &name);
QString temporaryDirectoryPattern(const QString &purpose);
QString platformName();

}
