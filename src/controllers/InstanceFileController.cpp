#include "InstanceFileController.hpp"

#include "InlineMessage.hpp"
#include "InstanceFilesPage.hpp"
#include "OverlayHost.hpp"
#include "manage/FolderManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

InstanceFileController::InstanceFileController(OverlayHost *overlay, Host host, QObject *parent)
    : QObject(parent), m_overlay(overlay), m_host(std::move(host)) {}

void InstanceFileController::log(const QString &text) const {
    if (m_host.log) m_host.log(text);
}

bool InstanceFileController::toggle(const QString &path, QString *problem) {
    const auto fail = [this, problem](const QString &text) {
        if (problem) *problem = text;
        else InlineMessage::warning(m_overlay, tr("改动失败"), text);
        return false;
    };
    const QFileInfo info(path);
    if (!info.exists())
        return fail(tr("「%1」现在不在盘上。").arg(QDir::toNativeSeparators(path)));
    if (info.isDir())
        return fail(tr("「%1」是一个文件夹，不能启用 / 禁用。\n\n"
                       "（把一个文件夹改了名，它里面的东西会一起从游戏眼里消失。）")
                        .arg(QDir::toNativeSeparators(path)));
    const QString suffix = InstanceFilesPage::disabledSuffix();
    const QString target = info.fileName().endsWith(suffix, Qt::CaseInsensitive)
                               ? path.left(path.size() - suffix.size()) : path + suffix;
    if (QFileInfo::exists(target))
        return fail(tr("「%1」已经有一个了 —— 先把那一份删掉或者改个名，再动这一份。")
                        .arg(QDir::toNativeSeparators(target)));
    if (!QFile::rename(path, target))
        return fail(tr("没能改「%1」的名字：多半是它正被游戏占着。先把游戏关掉再试。")
                        .arg(QDir::toNativeSeparators(path)));
    log(target.endsWith(suffix, Qt::CaseInsensitive)
            ? tr("禁用文件：%1").arg(QDir::toNativeSeparators(path))
            : tr("启用文件：%1").arg(QDir::toNativeSeparators(target)));
    return true;
}

bool InstanceFileController::remove(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        InlineMessage::warning(m_overlay, tr("删除失败"),
                               tr("「%1」现在不在盘上。").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    const QString question = info.isDir()
        ? tr("把文件夹「%1」连同里面的东西一起删掉？删了就没了。").arg(info.fileName())
        : tr("把「%1」从盘上删掉？删了就没了。").arg(info.fileName());
    if (!InlineMessage::question(m_overlay, tr("删除文件"), question, tr("删除"), tr("取消"),
                                 false, true)) return false;
    return removeUnasked(path);
}

bool InstanceFileController::removeUnasked(const QString &path, QString *problem) {
    const auto fail = [this, problem](const QString &text) {
        if (problem) *problem = text;
        else InlineMessage::warning(m_overlay, tr("删除失败"), text);
        return false;
    };
    const QFileInfo info(path);
    if (!info.exists())
        return fail(tr("「%1」现在不在盘上。").arg(QDir::toNativeSeparators(path)));
    const bool removed = info.isDir() ? removeTreeWithoutFollowingLinks(path) : QFile::remove(path);
    if (!removed)
        return fail(tr("没能删掉「%1」：多半是它正被游戏占着。先把游戏关掉再试。")
                        .arg(QDir::toNativeSeparators(path)));
    log(tr("删除文件：%1").arg(QDir::toNativeSeparators(path)));
    return true;
}

void InstanceFileController::toggleMany(const QStringList &paths, bool disable) {
    if (paths.isEmpty()) return;
    const QString suffix = InstanceFilesPage::disabledSuffix();
    int changed = 0, failed = 0;
    QStringList problems;
    for (const QString &path : paths) {
        if (path.endsWith(suffix, Qt::CaseInsensitive) == disable) continue;
        QString problem;
        if (toggle(path, &problem)) ++changed;
        else {
            ++failed;
            if (!problem.isEmpty() && !problems.contains(problem)) problems.append(problem);
        }
    }
    log(tr("%1：动了 %2 个文件。").arg(disable ? tr("批量禁用") : tr("批量启用")).arg(changed));
    if (failed > 0)
        InlineMessage::warning(m_overlay, tr("改动失败"),
                               tr("有 %1 个文件没能改动。\n\n%2")
                                   .arg(failed).arg(problems.join(QStringLiteral("\n"))));
}

void InstanceFileController::removeMany(const QStringList &paths) {
    if (paths.isEmpty()) return;
    const QString question = paths.size() == 1
        ? tr("把「%1」从盘上删掉？删了就没了。").arg(QFileInfo(paths.first()).fileName())
        : tr("把这 %1 个文件从盘上删掉？删了就没了。").arg(paths.size());
    if (InlineMessage::ask(m_overlay, MessageKind::Question, tr("删除文件"), question,
                           {tr("删除"), tr("取消")}, 1, 0) != 0) return;
    int removed = 0, failed = 0;
    QStringList problems;
    for (const QString &path : paths) {
        QString problem;
        if (removeUnasked(path, &problem)) ++removed;
        else {
            ++failed;
            if (!problem.isEmpty() && !problems.contains(problem)) problems.append(problem);
        }
    }
    log(tr("批量删除：删掉了 %1 个文件。").arg(removed));
    if (failed > 0)
        InlineMessage::warning(m_overlay, tr("删除失败"),
                               tr("有 %1 个文件没能删掉。\n\n%2")
                                   .arg(failed).arg(problems.join(QStringLiteral("\n"))));
}
