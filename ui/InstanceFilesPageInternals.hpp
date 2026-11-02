/**
 * @file InstanceFilesPageInternals.hpp
 * @brief `InstanceFilesPage` 的几个 .cpp 之间共用的**小工具**（目前只有一处）。
 *
 * 【为什么单独拿一个头文件】
 *
 * 这一页按"一摊状态一个翻译单元"拆成了几份：外壳与接线（含筛选、统计、选择条）、
 * 选择与批量操作（`InstanceFilesPageSelection.cpp`）、**行与行尾那几格**
 * （`InstanceFilesPageRows.cpp`）。`makeSecondary()` 原来住在主 .cpp 顶上的
 * **匿名命名空间**里 —— 匿名命名空间是内部链接，另一个 .cpp 根本看不见它。
 *
 * 搬过来、而不是拆开各抄一份：它改的是"次要文字取哪一档颜色"这一件事（全仓唯一去处是
 * `QPalette::PlaceholderText`），两处各抄一份的话，将来改一处、另一处继续用旧的那一档，
 * 屏幕上表现为"同一页里两种灰"，而代码里看不出哪一处才是对的。
 *
 * 【写法上的讲究】
 *
 * * 函数一律 `inline`：头文件里有定义，非 inline 的函数被两个 .cpp 各包含一次就是重定义
 *   （链接期 duplicate symbol，不是编译期）。
 *
 * NOTE: 这里面的东西**不属于** `InstanceFilesPage` 的接口，类外面也不许用 —— 它只解决
 * "同一个类的实现分散在几个文件里"这一个问题。
 */

#pragma once

#include <QLabel>
#include <QPalette>

namespace InstanceFilesPageInternals {

/// 比正文浅一档的那个颜色。**全仓唯一去处是 `QPalette::PlaceholderText`**（见
/// InlineMessage::setNote() 上面那段 NOTE）—— 这里只是把它挪到"路径"和"大小 / 时间"
/// 这两处次要文字上，不自己写死一个灰。
inline void makeSecondary(QLabel *label) {
    if (label == nullptr) {
        return;
    }
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
}

}  // namespace InstanceFilesPageInternals
