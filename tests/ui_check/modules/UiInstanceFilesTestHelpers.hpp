/**
 * @file UiInstanceFilesTestHelpers.hpp
 * @brief ui_check 里几个"造夹具"的小工具（跨模块共用一份，别各写一遍）。
 *
 * 为什么单独一层：`UiContentPagesChecks.cpp` 里原本有 `mkfile` / `mkfile2` / `mkfileB` /
 * `mkfileR` 四份内容一样的 lambda（都是"建目录 + 写一块字节"），拆出去的模块又各要一份。
 * 复制到第四遍的时候它就成了一份**会各自漂移的判据**：某一份改了写入字节数，另一份断言
 * 量的仍是旧值。所以收在这里，谁要谁 include。
 *
 * NOTE: 这些只是测试夹具，不碰产品代码；写失败**不报错**（和原来那几个 lambda 一样）——
 * 需要确认写成功的断言自己再 check 一次。
 */
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

/// 建出 @p path 的父目录并写 @p bytes 个 @p fill 字节（默认 4096 个 'x'）。
inline void uiMkFile(const QString &path, int bytes = 4096, char fill = 'x') {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray(bytes, fill));
    }
}
#include "InstanceFilesPage.hpp"

#include <QColor>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPalette>

// ---- 行里那几格怎么读 ----
//
// 「这一条叫什么」**不在 `item->text()` 上**：条目故意不带文字（带了的话视图代理会在透明的
// 行控件底下再画一遍同一个名字，屏幕上是重影；用户截图报的「模组名称出现了两次」就是它）。
// 名字的唯一出处是行里那一格标签，所以照着**界面上真正显示的东西**读。

/// 这一行的显示名（读行控件里那个名字标签）。
inline QString uiItemText(InstanceFilesPage *page, int row) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    QWidget *rowWidget =
        (page != nullptr && item != nullptr) ? page->list()->itemWidget(item) : nullptr;
    auto *label = rowWidget != nullptr
                      ? rowWidget->findChild<QLabel *>(InstanceFilesPage::nameLabelName())
                      : nullptr;
    return label != nullptr ? label->text() : QString();
}

/// 这一行在 @p role 上记的是不是真（禁用 / 可更新那些角色位）。
inline bool uiItemFlag(InstanceFilesPage *page, int row, int role) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr && item->data(role).toBool();
}

/// 这一行记的**盘上那个真路径**（禁用那条也带着 `.disabled` 后缀）。
inline QString uiItemPath(InstanceFilesPage *page, int row) {
    QListWidgetItem *item = page != nullptr ? page->list()->item(row) : nullptr;
    return item != nullptr ? item->data(InstanceFilesPage::pathRole()).toString() : QString();
}

/// 一格标签现在用的是哪种前景色（量"禁用那一条是不是灰的"用）。
inline QColor uiColorOf(QLabel *label) {
    return label != nullptr ? label->palette().color(QPalette::WindowText) : QColor();
}
