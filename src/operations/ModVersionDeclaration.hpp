#pragma once
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVersionNumber>
#include <limits>

// Conservative evaluation of package declarations, not a runtime compatibility guarantee.
namespace ModVersionDeclaration {
enum class Match { Yes, No, Unknown };
inline int compare(const QVersionNumber &left, const QVersionNumber &right) {
    return QVersionNumber::compare(left.normalized(), right.normalized());
}
inline bool numeric(const QString &text, QVersionNumber *out) {
    if (!QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+)*$")).match(text).hasMatch()) return false;
    for (const auto &segment : text.split('.')) { bool ok = false; segment.toInt(&ok); if (!ok) return false; }
    *out = QVersionNumber::fromString(text); return !out->isNull();
}
inline Match predicate(QString rule, const QString &target) {
    rule = rule.trimmed();
    if (rule == QStringLiteral("*")) return Match::Yes;
    if (!rule.isEmpty() && rule == target) return Match::Yes;
    QVersionNumber version;
    if (!numeric(target, &version)) return Match::Unknown;
    if (rule.startsWith('[') || rule.startsWith('(')) {
        if (!(rule.endsWith(']') || rule.endsWith(')'))) return Match::Unknown;
        const auto bounds = rule.mid(1, rule.size() - 2).split(',');
        if (bounds.size() == 1 && rule.startsWith('[') && rule.endsWith(']')) rule = bounds.first();
        else if (bounds.size() == 2) {
            for (const auto &bound : bounds) {
                QVersionNumber value;
                if (!bound.trimmed().isEmpty() && !numeric(bound.trimmed(), &value)) return Match::Unknown;
            }
            for (int side = 0; side < 2; ++side) {
                const QString bound = bounds.at(side).trimmed();
                if (bound.isEmpty()) continue;
                QVersionNumber value; if (!numeric(bound, &value)) return Match::Unknown;
                const int cmp = compare(version, value);
                if (side == 0 && (cmp < 0 || (cmp == 0 && rule.startsWith('(')))) return Match::No;
                if (side == 1 && (cmp > 0 || (cmp == 0 && rule.endsWith(')')))) return Match::No;
            }
            return Match::Yes;
        } else return Match::Unknown;
    }
    const auto tokens = rule.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return Match::Unknown;
    const QRegularExpression expression(QStringLiteral("^(>=|<=|>|<|=|~|\\^)?([0-9]+(?:\\.[0-9]+)*)(?:\\.([xX*]))?$"));
    for (const auto &token : tokens) {
        const auto match = expression.match(token); QVersionNumber number;
        if (!match.hasMatch() || !numeric(match.captured(2), &number)) return Match::Unknown;
        if (!match.captured(3).isEmpty() && !match.captured(1).isEmpty() && match.captured(1) != QStringLiteral("=")) return Match::Unknown;
    }
    for (const QString &token : tokens) {
        const auto m = expression.match(token);
        const QString op = m.captured(1);
        QVersionNumber value; if (!numeric(m.captured(2), &value)) return Match::Unknown;
        const int cmp = compare(version, value);
        if (!m.captured(3).isEmpty()) {
            for (int i = 0; i < value.segmentCount(); ++i) if (version.segmentAt(i) != value.segmentAt(i)) return Match::No;
        } else if (op == QStringLiteral("~") || op == QStringLiteral("^")) {
            auto upper = value.segments();
            const int count = static_cast<int>(upper.size());
            int bump = op == QStringLiteral("~") ? qMin(1, count - 1) : 0;
            if (op == QStringLiteral("^")) while (bump < count - 1 && upper.at(bump) == 0) ++bump;
            if (upper.at(bump) == std::numeric_limits<int>::max()) return Match::Unknown;
            ++upper[bump]; for (int i = bump + 1; i < count; ++i) upper[i] = 0;
            if (cmp < 0 || compare(version, QVersionNumber(upper)) >= 0) return Match::No;
        } else {
            const bool yes = op == QStringLiteral(">=") ? cmp >= 0 : op == QStringLiteral("<=") ? cmp <= 0
                : op == QStringLiteral(">") ? cmp > 0 : op == QStringLiteral("<") ? cmp < 0 : cmp == 0;
            if (!yes) return Match::No;
        }
    }
    return Match::Yes;
}
inline Match matches(const QJsonValue &rule, const QString &target) {
    if (target.isEmpty()) return Match::Unknown;
    if (rule.isString()) return predicate(rule.toString(), target);
    if (!rule.isArray() || rule.toArray().isEmpty()) return Match::Unknown;
    for (const auto &entry : rule.toArray()) if (!entry.isString()) return Match::Unknown;
    bool unknown = false;
    for (const auto &entry : rule.toArray()) {
        const auto result = matches(entry, target);
        if (result == Match::Yes) return result;
        if (result == Match::Unknown) unknown = true;
    }
    return unknown ? Match::Unknown : Match::No;
}
}
