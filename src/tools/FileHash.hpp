/**
 * @file FileHash.hpp
 * @brief 文件哈希：算一个文件的摘要、读懂用户粘进来的"已知值"、比出个结论。
 * @note 用户 点名的工具箱第一件（coding plan §9.11 第 1 项）：「文件哈希校验：
 * SHA-256 / SHA-1 / MD5、拖入文件、复制结果、与已知值对照」。
 *
 * 【为什么要自己算，而不是让用户去装个工具】
 *
 * 「这个 jar 是不是我下的那一个」「这个整合包被人改过没有」—— 这类问题在启动器里天天遇到
 * （补全文件、校验版本、比对模组），而答案就在用户手上那个文件里。工具箱这一件不做别的
 * 事，就是把这件事单独摆出来：拖进来、选一种、粘上人家给的那一串、看一致不一致。
 *
 * 【"已知值"那一格为什么不能直接字符串比】
 *
 * 各处的摘要写法都不一样：下载页给人看的是大写带连字符（`A1-B2-…`）、Modrinth 是裸的小写、
 * 有的地方写成 `sha256:abc…`、从网页上复制还常常带换行和空格。**这些都是同一串十六进制**，
 * 直接比字符串会把它们当成不同的东西 —— 用户看到"不一致"而去重新下载一个其实是好的文件。
 * 所以这里只做一件事：把输入归一化（去空白、去 `-` `:`、统一小写、认出算法前缀），再比
 * 十六进制本身。
 *
 * 【算大文件为什么要有进度和取消】
 *
 * 一个整合包里的 jar 几 MB 到几百 MB 都有，摘要要把**每一个字节**都读一遍。整份读进内存
 * 既浪费又危险（几百 MB 的分配），所以这一层是**流式**的：一块一块读、一块一块喂给
 * `QCryptographicHash`，顺手把进度报出去、也留一个"用户点了取消"的出口。
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

namespace FileHash {

/// 三档。**顺序就是界面上那一排的次序**（从最长到最短，SHA-256 是大家默认要的那一个）。
enum class Algorithm { Sha256, Sha1, Md5 };

/// 界面上显示的名字（`SHA-256` / `SHA-1` / `MD5`）。
QString algorithmName(Algorithm algorithm);
/// 配置 / 命令行那种小写名字（`sha256` / `sha1` / `md5`）。
QString algorithmKey(Algorithm algorithm);
/// 三档全在表里，界面按它摆（**不许在界面里另写一份**）。
QList<Algorithm> algorithms();
/// 这一档算出来的十六进制有几位（32 / 40 / 64）。
int hexLength(Algorithm algorithm);

/// 一段字节的摘要（小文件、测试、以及"给个已知值自己算"都用它）。
QString digestOf(const QByteArray &bytes, Algorithm algorithm);

/// 算一个文件要读多少块（一块 1 MiB：几 MB 的 jar 也就几趟，几百 MB 也占不了多少内存）。
constexpr int kChunkBytes = 1024 * 1024;

/// 算出来的结果（**成功和"为什么没成功"都在里面**，同 ServerPing::Status 那条规矩）。
struct Result {
    bool ok = false;
    /// 没成功的原因（中文，直接显示）。
    QString error;
    /// 小写十六进制。ok 为假时是空串。
    QString hex;
    /// 这个文件多大（字节）。ok 为假时是 -1。
    qint64 bytes = -1;
};

/// 进度回调：已经读了多少字节、一共多少字节（一共那个在算之前就问好了；问不出来是 -1）。
using ProgressFn = std::function<void(qint64 done, qint64 total)>;
/// 取消回调：回 true 就停（结果里 error 写「已取消」）。
using CancelFn = std::function<bool()>;

/// 算一个文件（流式）。
Result digestOfFile(const QString &path, Algorithm algorithm);
/// 带进度和取消的那一份（界面上那条进度条走的就是它）。
Result digestOfFile(const QString &path, Algorithm algorithm, const ProgressFn &progress,
                    const CancelFn &cancelled = {});

/**
 * @brief 一个文件的"这一刻长什么样"（大小 + 修改时间）。
 *
 * 用途只有一个：**算完之后文件被改过没有**（coding plan §9.11 第 1 项那条"文件变化使结果
 * 失效"）。摘要算的是当时那一刻的字节；文件被重新下载、被更新器换掉之后，屏幕上那串十六
 * 进制就不是"这个文件现在的摘要"了 —— 这时必须说出来，而不是让用户拿它去对。
 *
 * NOTE: 只看大小和修改时间，不重算摘要（重算就回到"要再读一遍几百 MB"那条路上去了）。
 * 所以这是**近似**判据：改了内容又把大小和修改时间都摆回原样的情况认不出来 ——
 * 那种情况下"文件变过"本来就不该由这个工具来断言。
 */
struct Stamp {
    bool ok = false;          ///< 读到了（文件在、读得动）
    qint64 bytes = -1;        ///< 多大（读不到时 -1）
    qint64 lastModifiedMs = -1;///< 修改时间（毫秒时间戳；读不到时 -1）
};

/// 现在这一刻的 @p path 长什么样。
Stamp stampOf(const QString &path);
/// 两份记录说的是不是同一个状态（**两个都 `ok` 才算一样**）。
bool sameStamp(const Stamp &left, const Stamp &right);

/**
 * @brief 用户粘进来的"已知值"。
 *
 * 认这些写法（大小写、空白、`-`、`:`、`sha256:` 前缀都不计较）：
 * ```
 * 3f79bb7b435b05321651daefd374cdc681dc06faa65e374e38337b88ca046dea
 * 3F79BB7B435B05321651DAEFD374CDC681DC06FAA65E374E38337B88CA046DEA
 * 3f79 bb7b 435b 0532 … ← 从网页上复制带换行/空格
 * 3f-79-bb-7b-… ← 带连字符
 * sha256:3f79bb7b… ← 带算法前缀
 * SHA-256 = 3f79bb7b…
 * ```
 */
struct Expected {
    /// 认出来了（归一化之后是一串合法的十六进制，而且长度对得上某一档）。
    bool valid = false;
    /// 归一化之后的十六进制（小写、没有分隔符）。
    QString hex;
    /// 输入里有没有点名算法（`sha256:` / `SHA-1 =`）。
    bool hasAlgorithm = false;
    Algorithm algorithm = Algorithm::Sha256;
    /// 没认出来的原因（中文）。valid 为真时是空串。
    QString error;
};

/// 读那串"已知值"。
Expected parseExpected(const QString &text);

/// 比一下。
struct Verdict {
    /// 比出来了（expected 是合法的）。
    bool valid = false;
    bool matches = false;
    /// 一句中文结论（直接显示）。
    QString text;
    /// 已知值点名的那一档（`parseExpected` 里读出来的）。比不同档的摘要没有意义，
    /// 所以调用方拿它和"用户选的这一档"对一下，不一致就该提示。
    Algorithm expectedAlgorithm = Algorithm::Sha256;
    bool expectedHasAlgorithm = false;
};

/// @param actualHex 我们算出来的摘要（哪一档由调用方决定）。
Verdict compare(const QString &actualHex, const Expected &expected);

/// 文件多大写成 `12.3 MB` 这种（界面上那一行用）。
QString fileSizeText(qint64 bytes);

}  // namespace FileHash
