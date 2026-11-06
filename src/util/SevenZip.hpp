/**
 * @file SevenZip.hpp
 * @brief 找本机的 7-Zip，并把 `.rar` / `.7z` 解出来（整合包导入的前置步骤）。
 *
 * 只调用**已安装的** `7z.exe`（PATH、常见安装目录、注册表 InstallLocation 三处找），
 * 不捆绑、不下载。找不到就如实说"没装 7-Zip"，让用户先解压。
 */
#pragma once

#include <QString>

namespace SevenZip {

/// 找到的 7z.exe 绝对路径；空 = 没装。
QString executable();

bool available();

/// 这些后缀需要 7-Zip 才能拆（zip/mrpack 走内建那条）。
bool isArchive(const QString &path);

/// 解到临时目录再压成一份 zip，返回那份 zip 的路径（调用方负责删）；失败返回空。
/// @param error 失败原因（人话）。
QString toZip(const QString &archive, QString *error);

}  // namespace SevenZip
