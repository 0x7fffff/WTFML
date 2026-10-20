#include "InstanceFileController.hpp"

#include "InlineMessage.hpp"
#include "InstanceFilesPage.hpp"
#include "OverlayHost.hpp"
#include "manage/FolderManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <utility>

namespace {

/// 一条 junction / 符号链接。**两个都要问**：`isSymLink()` 对 junction 是 `false`
/// （见 [[wtfml-junction-facts]]），只判那头会把 junction 漏成一个普通文件夹。
bool isLinkEntry(const QFileInfo &info) {
    return info.isSymLink() || info.isJunction();
}

/// 单条删除那一问。★ 2026-10-20：链接**不能**按文件夹写 —— `removeUnasked()` 对目录走
/// `removeTreeWithoutFollowingLinks()`，它碰到链接只摘掉链接本身、指向的东西一个字节都不动；
/// 而 `QFileInfo::isDir()` 对 junction 是 `true`。原来那一问照着 `isDir()` 写，用户看到的是
/// 「连同里面的东西一起删掉」，实际删掉的是个两字节的壳子 —— 反过来更糟：用户以为链接背后
/// 那棵大树还在、其实（如果他自己以为是删文件）也没有。说什么就得做什么。
QString removalQuestionFor(const QFileInfo &info) {
    const QString name = info.fileName();
    if (isLinkEntry(info)) {
        return QObject::tr("把「%1」这个链接删掉？只删链接本身，它指向的东西一个都不会动。")
            .arg(name);
    }
    if (info.isDir()) {
        return QObject::tr("把文件夹「%1」连同里面的东西一起删掉？删了就没了。").arg(name);
    }
    return QObject::tr("把「%1」从盘上删掉？删了就没了。").arg(name);
}

}  // namespace

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
    const QString question = removalQuestionFor(info);
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
    // ★ 2026-10-20：两条路都走 `removeTreeWithoutFollowingLinks()`。它对**文件**多做了
    //   一件 `QFile::remove()` 不做的事：失败时先摘掉只读位再试一次（见 FolderManager.cpp
    //   里那段）—— 从只读介质拷来的 jar、被网盘/别的启动器标成只读的整合包，不然永远删不掉，
    //   而下面那句提示还把人往"游戏占着"引，关掉游戏也删不掉。
    //   目录那条路本来就是它（连里面一起删、不追链接）。
    const bool wasDirectory = info.isDir() && !isLinkEntry(info);
    const bool removed = removeTreeWithoutFollowingLinks(path);
    if (!removed)
        return fail(wasDirectory
            ? tr("没能把「%1」删干净：多半是里面的某个文件正被游戏占着。\n\n"
                 "已经删掉的那部分找不回来了，剩下的还在原处。先把游戏关掉，再删一次。")
                  .arg(QDir::toNativeSeparators(path))
            : tr("没能删掉「%1」：多半是它正被游戏占着。先把游戏关掉再试。")
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
    // ★ 2026-10-20：这一批里**可能有文件夹**。资源包 / 数据包 / 光影包那三页连目录一起列
    //   （未压缩的目录包正是游戏认可的一种形态），而 `removeUnasked()` 对目录是**连里面的
    //   东西一起递归删**（`removeTreeWithoutFollowingLinks`）。原来这一问无论收到什么都
    //   写「这 N 个文件」—— 唯一的确认框把实际破坏范围说小了，用户以为只删一个壳子。
    //   单条那条路（`remove()`）早就分开写了，批量这条路漏了同一件事。
    // 链接单独数：它们**不**是"里面的东西一起删"，只摘壳子（见 `removalQuestionFor()`）。
    int dirCount = 0;
    int linkCount = 0;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (isLinkEntry(info)) ++linkCount;
        else if (info.isDir()) ++dirCount;
    }
    const QString question = paths.size() == 1
        ? removalQuestionFor(QFileInfo(paths.first()))
        : (linkCount == paths.size()
               ? tr("把这 %1 个链接删掉？只删链接本身，它们指向的东西一个都不会动。")
                     .arg(paths.size())
               : (dirCount == 0 && linkCount == 0
                      ? tr("把这 %1 个文件从盘上删掉？删了就没了。").arg(paths.size())
                      : (linkCount == 0 && dirCount == paths.size()
                             ? tr("把这 %1 个文件夹连同里面的东西一起删掉？删了就没了。")
                                   .arg(paths.size())
                             : [&] {
                                   QString text =
                                       tr("把这 %1 项从盘上删掉？").arg(paths.size());
                                   if (dirCount > 0)
                                       text += tr("其中 %1 个是文件夹（连同里面的东西一起删）")
                                                   .arg(dirCount);
                                   if (linkCount > 0) {
                                       if (dirCount > 0) text += tr("，");
                                       text += tr("%1 个是链接（只删链接本身，指向的东西不动）")
                                                   .arg(linkCount);
                                   }
                                   return text + tr("。删了就没了。");
                               }())));
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
    // 「项」而不是「个文件」：这一批里可能混着文件夹（见上面那一问）。
    log(tr("批量删除：删掉了 %1 项。").arg(removed));
    if (failed > 0)
        InlineMessage::warning(m_overlay, tr("删除失败"),
                               tr("有 %1 项没能删掉。\n\n%2")
                                   .arg(failed).arg(problems.join(QStringLiteral("\n"))));
}
