#include "Support.hpp"

void runProcessPathLimitTests() {
    section("ZPL. 起进程那条 260 字符的路径墙（只解释，不拦）");

    // --- ZPL1. 判据本身：边界是 259 / 260 ------------------------------------
    check(!ProcessPathLimit::tooLong(QString()), "ZPL1 空串不算长（\"没配路径\"不是路径太长）");
    check(!ProcessPathLimit::tooLong(QStringLiteral("C:/Program Files/Java/jdk-21/bin/java.exe")),
          "ZPL1 一条普通路径不算长");
    check(!ProcessPathLimit::tooLong(QString(259, QLatin1Char('a'))),
          "ZPL1 ★ 259 个字符放得下（260 含结尾 NUL）");
    check(ProcessPathLimit::tooLong(QString(260, QLatin1Char('a'))),
          "ZPL1 ★ 260 个字符就放不下了 —— 这一格是半边界的另一半");

    // --- ZPL2. 提示：没超长就一个字都不加 -----------------------------------
    check(ProcessPathLimit::tooLongHint({}).isEmpty(), "ZPL2 一条路径都没给 ⇒ 不加话");
    check(ProcessPathLimit::tooLongHint({QStringLiteral("C:/MC/java.exe")}).isEmpty(),
          "ZPL2 都不超长 ⇒ 不加话");
    check(ProcessPathLimit::tooLongHint({QString(259, QLatin1Char('a'))}).isEmpty(),
          "ZPL2 ★ 边界 259 也不加（多一句反而让人以为路径有问题）");

    // --- ZPL3. 超长时那句话说清三件事：哪条、多长、上限多少 ----------------
    const QString deep =
        QStringLiteral("C:/MC/") + QString(300, QLatin1Char('x')) + QStringLiteral("/java.exe");
    const QString hint = ProcessPathLimit::tooLongHint({deep});
    check(!hint.isEmpty(), "ZPL3 超长 ⇒ 有话说");
    check(hint.contains(QDir::toNativeSeparators(deep)),
          "ZPL3 ★ 提示里是**完整**那条路径（截断的话用户没法照着改）", hint);
    check(hint.contains(QString::number(deep.size())), "ZPL3 提示里报出实际字符数", hint);
    check(hint.contains(QStringLiteral("260")), "ZPL3 提示里报出那条上限", hint);
    check(hint.contains(QStringLiteral("D:/MC")),
          "ZPL3 给出一条能照着做的出路（挪到浅一点的目录）", hint);

    // --- ZPL4. 程序和工作目录同时超长时，指向更深的那个 ----------------------
    const QString deeper =
        QStringLiteral("C:/MC/") + QString(360, QLatin1Char('y')) + QStringLiteral("/java.exe");
    const QString both = ProcessPathLimit::tooLongHint(
        {deep, QStringLiteral("C:/MC/java.exe"), deeper});
    check(both.contains(QDir::toNativeSeparators(deeper)),
          "ZPL4 ★ 两条都超长 ⇒ 挑最长的（用户该改的就是它）", both);
    check(!both.contains(QDir::toNativeSeparators(deep)),
          "ZPL4 只提一条：两条都列出来只会让人不知道该动哪个", both);
    check(both.contains(QString::number(deeper.size())), "ZPL4 报的字符数跟着最长那条走");

    // --- ZPL5. 分隔符：提示里给的是本机习惯的那一种 -------------------------
    const QString slashed = QStringLiteral("C:/MC/") + QString(280, QLatin1Char('z'));
    const QString native = QDir::toNativeSeparators(slashed);
    check(ProcessPathLimit::tooLongHint({slashed}).contains(native),
          "ZPL5 提示里的路径按本平台的分隔符显示（Windows 上反斜杠）",
          ProcessPathLimit::tooLongHint({slashed}));
    // 长度按字符算，换不换分隔符都一样（'/' 和 '\\' 都是一个字符）—— 这一条盯的是
    // "有人顺手先 toNativeSeparators 再判长度"那种改法不会把边界挪掉。
    check(ProcessPathLimit::tooLong(native) == ProcessPathLimit::tooLong(slashed),
          "ZPL5 换分隔符不改变超没超长");
}
