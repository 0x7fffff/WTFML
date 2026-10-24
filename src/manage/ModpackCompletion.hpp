#pragma once
#include "ModpackImport.hpp"
#include "VersionIntegrity.hpp"
#include <functional>

/// 只有核心、加载器、清单下载和覆盖解压均结束后才允许建立凭据。
/// 调用方持有数据目录写锁；文件分块校验、取消不放行，旧报告不升级为凭据。
namespace ModpackCompletion {
using Stop = std::function<bool()>;
bool capture(const QString &instance, const QString &dataRoot, const ModpackImport::Pack &pack,
             const QString &fingerprint, const QList<VersionIntegrity::ExpectedFile> &core,
             QString *error, const Stop &stop);
bool finish(const QString &instance, QString *error, const Stop &stop);
}
