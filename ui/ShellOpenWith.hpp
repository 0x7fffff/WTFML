/**
 * @file ShellOpenWith.hpp
 * @brief 调 Windows 自己的「打开方式」对话框。
 *
 * 只做入口，不自己执行文件：选中哪个程序、拿什么打开，全交给系统（`SHOpenWithDialog`）。
 * 这样既不会写关联、也不会拉起任意 jar —— 行为和资源管理器里右键「打开方式」一样。
 */
#pragma once

#include <QString>

class QWidget;

namespace ShellOpenWith {

/// @return 用户选了一个程序并确认了没有（取消也返回 false）。
bool openWithDialog(QWidget *owner, const QString &path);

}  // namespace ShellOpenWith
