#pragma once
#include "ModpackImport.hpp"
#include <functional>

namespace ModpackInstallReport {
struct Report {
    QString packName;
    QString failureReason;
    bool complete = false;
    QList<ModpackImport::FileIssue> issues;
};
/// 仅接受实例目录内的有界、已知版本报告；失败不返回部分数据。
bool read(const QString &instanceDir, Report *report, QString *problem);
/// 调用者持有写锁；按大小和 SHA-1 校验后原子发布。不覆盖已有不同内容。
/// cancelled 同时可用于截止时间检查；源文件始终只读。
bool supply(const QString &instanceDir, const ModpackImport::FileIssue &issue,
            const QString &source, QString *problem,
            const std::function<bool()> &cancelled = {});
/// 只读核对手动补入的文件；缺少强校验、目录越界或链接文件一律保留缺项。
QList<ModpackImport::FileIssue> remaining(
    const QList<ModpackImport::FileIssue> &issues, const QString &instanceDir);
QString description(const ModpackImport::FileIssue &issue);
/// 在实例中原子写入持久报告；调用者须已持有实例写锁。
bool write(const QString &instanceDir, const ModpackImport::Pack &pack,
           const QList<ModpackImport::FileIssue> &issues, bool complete, QString *problem,
           const QString &failureReason = {});
}
