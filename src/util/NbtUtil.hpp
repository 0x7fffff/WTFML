/**
 * @file NbtUtil.hpp
 * @brief 读 Minecraft 的 servers.dat（NBT 格式）里的服务器列表。
 * @note 项目没有引入任何 NBT 库，这里只实现「读到能取出 name / ip 两格就够」的
 * 最小读取器。**故意不做通用 NBT 读写**：多写一个能改文件的写入器，就等于
 * 多一个能把用户的服务器列表写坏的入口，而启动器对这个文件只需要读。
 *
 * 【这个文件存在的理由】
 *
 * 用户的服务器列表是游戏自己维护的（在游戏里加服务器，写进 <数据目录>/servers.dat），
 * 而启动器要在自己的界面里把这份列表显示出来。格式是 NBT，所以这里带一个读取器。
 *
 * 【两个真机上都会遇到的形态】
 *
 * 同一个文件有两种落盘方式，**只能按文件头两个字节分**，不能按版本猜：
 * 前两个字节是 `0x1F 0x8B` 就是 gzip（游戏早期版本与多数第三方工具写的），
 * 否则当成裸 NBT（未压缩）。两种都要认。
 *
 * NOTE: gzip 那一档**不引 zlib**（理由见 ZipUtil.hpp 开头那段），走 PowerShell +
 * .NET 解到临时文件再读回来 —— 不让 PowerShell 把字节打到 stdout，理由和
 * ZipUtil::readEntry 里那段一样：stdout 是文本通道，二进制过一趟就不再是原来的字节。
 */
#pragma once

#include <QList>
#include <QString>

namespace NbtUtil {

/// `servers.dat` 里的一条服务器。
struct ServerEntry {
    QString name;     ///< 显示名（列表里那一行字）
    QString address;  ///< `ip` 那一格：可能是 `host` 也可能是 `host:port`
};

enum class Status {
    Ok,          ///< 读到了（**一条都没有也算 Ok**，见下）
    Missing,     ///< 盘上没有这个文件
    Unreadable,  ///< 有文件但读不动：不是 NBT / 坏了 / 解压失败，原因在 problemOut
};

/**
 * @brief 读一个 servers.dat 里的服务器列表。
 * @param datPath 文件绝对路径（`<数据目录>/servers.dat`）。
 * @param out 成功时写入读到的条目（**一律先 clear**）；失败时保持空。**不要求非空**。
 * @param problemOut 非空时，Unreadable 那条路会把给用户看的原因写进来（其余情况清空）。
 * @return 见 Status。三种结果的分别是有意义的：**没有这个文件**和**这个文件读不动**
 * 是两句话（前者是「还没在游戏里加过服务器」，后者才是「这份文件有问题」），
 * 而且**空列表是 Ok** —— 盘上有一份好好的 servers.dat、里面一条都没有，
 * 那是完全正常的状态，不是错误。
 *
 * 解析规则（就这几条，其余一律 Unreadable）：
 * - 根必须是 TAG_Compound；
 * - 根里那一格 `servers` 必须存在且是 TAG_List，元素类型必须是 TAG_Compound；
 * - 每格元素里取 `name` / `ip` 两个 TAG_String。`ip` 就是**地址那一格的名字**
 * （不是 `address`）；元素里别的东西（`hidden` / `icon` / 未来的新字段）一律跳过。
 *
 * NOTE: `servers` 那一格**可以不存在**（早期版本/第三方工具写出来的就是没有它），
 * 那和空 list 一样按「一条都没有」处理 —— 都是 Ok。要是按 Unreadable 报，
 * 用户看到的就是「你的服务器列表坏了」，而其实只是没有。
 * NOTE: 取不到 `ip`（那一格不存在、是空串、或者类型不是字符串）的元素**不返回**：
 * 地址是这一条唯一的实质内容，没有它这行在界面上只是一格点不动的空气。
 * `name` 取不到照样返回（地址还在），name 留空串，显示名由调用方去兜底。
 * NOTE: 任何长度字段离谱（NBT 里每个长度都是 int32，文件里写 0x7FFFFFFF 是**合法字节**）
 * 都在**读之前**拿剩余字节数卡住，不会照着它分配内存、也不会转二十亿圈。
 * 文件先看 QFileInfo::size()，超过 16 MB 直接 Unreadable（真机上是几百字节）。
 * NOTE: gzip 那一档失败（没 PowerShell / 超时 / 不是 gzip）**不重试**，直接 Unreadable
 * 并把原因写进 problemOut —— 重试只会让用户多等 15 秒拿到同一句错。
 */
Status readServerList(const QString &datPath, QList<ServerEntry> *out, QString *problemOut);

}  // namespace NbtUtil
