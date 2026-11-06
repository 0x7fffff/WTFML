/**
 * @file FileAssociations.hpp
 * @brief 把 MC 相关文件类型挂到本启动器（**只写 HKCU**，不需要管理员）。
 *
 * 分两档：
 * - `.nbt` / `.litematic` / `.mrpack` 是 MC 独有的：可以**设为默认**；
 * - `.jar` / `.zip` 被别的程序用着（压缩包、Java 程序）：只登记「打开方式」候选，
 *   **绝不改默认**。
 *
 * 注册表形状（和资源管理器认的一致）：
 *   HKCU\Software\Classes\.nbt                    (默认) = WTFML.nbt
 *   HKCU\Software\Classes\WTFML.nbt\shell\open\command (默认) = "<exe>" "%1"
 *   HKCU\Software\Classes\.jar\OpenWithProgids\WTFML.jar = ""
 *   HKCU\Software\Classes\WTFML.jar\shell\open\command  = "<exe>" "%1"
 */
#pragma once

#include <QString>
#include <QStringList>

namespace FileAssociations {

/// 能被本启动器打开的后缀（小写、带点）。
QStringList openableExtensions();

/// MC 独有、可以设为默认的那几个。
QStringList mcOnlyExtensions();

bool isOpenable(const QString &extension);
bool isMcOnly(const QString &extension);

/// 把 @p extension 的默认打开方式改成 WTFML（只对 MC 独有的那几个有意义）。
bool registerDefault(const QString &extension, QString *error = nullptr);
/// 只登记「打开方式」候选，不动默认（`.jar` / `.zip` 用这一条）。
bool registerOpenWith(const QString &extension, QString *error = nullptr);
/// 现在默认打开方式是不是 WTFML。
bool isDefaultFor(const QString &extension);

/// 一次性把 MC 独有的那几个设成默认；返回设成功几个。
int registerMcDefaults(QStringList *errors = nullptr);

/// 自检（只写测试子键，见 WTFML_ASSOC_CLASSES_ROOT）：返回逐行结果，空 = 全过。
QString runSelfTest();

}  // namespace FileAssociations

