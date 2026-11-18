/**
 * @file FileAssociations.hpp
 * @brief 清掉旧版本注册过的文件关联（**只清本程序写的那几条**，不写新的）。
 *
 * 0.5.x 曾把 `.nbt` / `.litematic` / `.schematic` / `.schem` / `.mrpack` 设成默认打开方式，
 * 并把 `.jar` / `.zip` 挂进「打开方式」候选。本程序不再注册关联，启动时把旧版留下的那几条清掉。
 *
 * 旧版写下的注册表形状（本文件只认这几处，也只清这几处）：
 *   HKCU\Software\Classes\WTFML<ext>\shell\open\command（整个 ProgID 键）
 *   HKCU\Software\Classes\.<ext> 的默认值 = WTFML<ext> 时清掉这个值
 *   HKCU\Software\Classes\.<ext>\OpenWithProgids 下 WTFML<ext> 这个值
 *
 * 别人的关联、用户自己设的默认打开方式一律不动，`.<ext>` 键本身也不删。
 *
 * 测试根：环境变量 `WTFML_ASSOC_CLASSES_ROOT`（相对 `HKEY_CURRENT_USER` 的路径）指到哪儿，
 * 所有读写就落在哪儿。自检只认它，没设就不跑，绝不碰真实的 `HKCU\Software\Classes`。
 */
#pragma once

#include <QString>
#include <QStringList>

namespace FileAssociations {

/// 旧版能注册关联、也是本程序能从命令行/拖放接住的后缀（小写、带点）。
QStringList openableExtensions();

bool isOpenable(const QString &extension);

/// 清掉旧版注册的这几条，返回清掉的项数：0 = 本来就没有可清的。
///
/// 重复调用安全：第二次开始没有可清的项，返回 0、不报错、不弹窗。真清掉东西时才调一次
/// `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)` 让资源管理器重读关联。
/// @p problems 收集读写注册表时的意外失败（不是"别人的项没动"这种正常结果）。
int cleanupLegacyAssociations(QStringList *problems = nullptr);

/// 自检（只写 `WTFML_ASSOC_CLASSES_ROOT` 指的测试子键）：返回逐行结果，空 = 全过。
///
/// 覆盖：只清本程序注册的、别人的原样留着、重复跑没有副作用、`.<ext>` 键本身不删。
QString runSelfTest();

}  // namespace FileAssociations
