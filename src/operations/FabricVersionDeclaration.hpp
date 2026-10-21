/**
 * @file FabricVersionDeclaration.hpp
 * @brief Fabric 扩展语义版本范围，包含空预发布下界；不用于 Maven 区间。
 */
#pragma once
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QVersionNumber>
#include <limits>
#include <optional>

namespace FabricVersionDeclaration {
enum class Match { Yes, No, Unknown };
struct Version {
    QVersionNumber number;
    QString pre;
    bool prerelease = false;
};
inline std::optional<Version> parse(const QString &text) {
    static const QRegularExpression expression(QStringLiteral(
        "^([0-9]+(?:\\.[0-9]+)*)(?:-([0-9A-Za-z.-]*))?(?:\\+([0-9A-Za-z.-]+))?$"));
    const auto m = expression.match(text);
    if (!m.hasMatch()) return {};
    for (const auto &part : m.captured(1).split('.')) {
        bool ok = false; part.toInt(&ok); if (!ok) return {};
    }
    const bool pre = m.capturedStart(2) >= 0;
    if (pre && !m.captured(2).isEmpty())
        for (const auto &part : m.captured(2).split('.')) if (part.isEmpty()) return {};
    if (m.capturedStart(3) >= 0)
        for (const auto &part : m.captured(3).split('.')) if (part.isEmpty()) return {};
    return Version{QVersionNumber::fromString(m.captured(1)), m.captured(2), pre};
}
inline int compare(const Version &a, const Version &b) {
    const int core = QVersionNumber::compare(a.number.normalized(), b.number.normalized());
    if (core) return core;
    if (a.prerelease != b.prerelease) return a.prerelease ? -1 : 1;
    if (!a.prerelease || a.pre == b.pre) return 0;
    if (a.pre.isEmpty() || b.pre.isEmpty()) return a.pre.isEmpty() ? -1 : 1;
    const auto left = a.pre.split('.'), right = b.pre.split('.');
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    for (qsizetype i = 0; i < qMin(left.size(), right.size()); ++i) {
        const bool ln = digits.match(left[i]).hasMatch(), rn = digits.match(right[i]).hasMatch();
        if (ln != rn) return ln ? -1 : 1;
        QString l = left[i], r = right[i];
        if (ln) {
            while (l.size() > 1 && l.startsWith('0')) l.remove(0, 1);
            while (r.size() > 1 && r.startsWith('0')) r.remove(0, 1);
            if (l.size() != r.size()) return l.size() < r.size() ? -1 : 1;
        }
        const int cmp = QString::compare(l, r, Qt::CaseSensitive);
        if (cmp) return cmp;
    }
    return left.size() < right.size() ? -1 : 1;
}
inline Match predicate(const QString &rule, const QString &target) {
    const auto tokens = rule.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty() || target.isEmpty()) return Match::Unknown;
    const auto actual = parse(target);
    bool no = false;
    for (const auto &token : tokens) {
        if (token == QStringLiteral("*")) continue;
        static const QRegularExpression tokenExpression(QStringLiteral("^(>=|<=|>|<|=|~|\\^)?(.+)$"));
        const auto m = tokenExpression.match(token);
        if (!m.hasMatch()) return Match::Unknown;
        const QString op = m.captured(1), value = m.captured(2);
        if ((op.isEmpty() || op == QStringLiteral("=")) && value == target) continue;
        if (!actual) {
            if ((op.isEmpty() || op == QStringLiteral("=")) && !value.contains('|')) { no = true; continue; }
            return Match::Unknown;
        }
        static const QRegularExpression wildcard(QStringLiteral("^([0-9]+(?:\\.[0-9]+)*)(?:\\.[xX*])+$"));
        const auto wild = wildcard.match(value);
        auto lower = parse(wild.hasMatch() ? wild.captured(1) + '-' : value);
        if (!lower) return Match::Unknown;
        const int cmp = compare(*actual, *lower);
        if (wild.hasMatch() || op == QStringLiteral("~") || op == QStringLiteral("^")) {
            if (wild.hasMatch() && !op.isEmpty() && op != QStringLiteral("=")) return Match::Unknown;
            auto upper = lower->number.segments();
            const int count = int(upper.size());
            const int bump = wild.hasMatch() ? count - 1 : op == QStringLiteral("^") ? 0 : qMin(1, count - 1);
            if (upper[bump] == std::numeric_limits<int>::max()) return Match::Unknown;
            ++upper[bump]; for (int i = bump + 1; i < count; ++i) upper[i] = 0;
            if (cmp < 0 || compare(*actual, Version{QVersionNumber(upper), {}, true}) >= 0) no = true;
        } else {
            const bool yes = op == QStringLiteral(">=") ? cmp >= 0 : op == QStringLiteral("<=") ? cmp <= 0
                : op == QStringLiteral(">") ? cmp > 0 : op == QStringLiteral("<") ? cmp < 0 : cmp == 0;
            if (!yes) no = true;
        }
    }
    return no ? Match::No : Match::Yes;
}
inline Match matches(const QJsonValue &rule, const QString &target) {
    if (rule.isString()) return predicate(rule.toString(), target);
    if (!rule.isArray() || rule.toArray().isEmpty()) return Match::Unknown;
    for (const auto &entry : rule.toArray()) if (!entry.isString()) return Match::Unknown;
    bool unknown = false;
    for (const auto &entry : rule.toArray()) {
        if (!entry.isString()) return Match::Unknown;
        const auto result = predicate(entry.toString(), target);
        if (result == Match::Yes) return result;
        unknown |= result == Match::Unknown;
    }
    return unknown ? Match::Unknown : Match::No;
}
}
