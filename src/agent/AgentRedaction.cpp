/**
 * @file AgentRedaction.cpp
 * @brief 凭据形态的扫描规则（字段名优先，其次常见令牌形态）。
 */

#include "AgentRedaction.hpp"

#include <QRegularExpression>

namespace AgentRedaction {
namespace {

QString replacementText() { return QStringLiteral("<redacted>"); }

void note(Result *result, const QString &kind) {
    result->hits += 1;
    if (!result->kinds.contains(kind)) {
        result->kinds.append(kind);
    }
}

const QRegularExpression &authorizationPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"((?i)\b(authorization\s*[:=]\s*)(bearer\s+)?([A-Za-z0-9._~+/=-]{8,}))"));
    return pattern;
}

const QRegularExpression &fieldPattern() {
    // 只遮“字段名 = 值”里的值；值本身必须够长，避免把 `password = true` 这种说明文字遮掉。
    static const QRegularExpression pattern(QStringLiteral(
        R"((?i)\b(access_token|refresh_token|id_token|password|passwd|secret|client_secret|api_key|apikey|cookie|set-cookie|csrf|session(?:_id|token)?)\b(\s*[:=]\s*["']?)([^\s"',;]{6,}))"));
    return pattern;
}

const QRegularExpression &bearerPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"((?i)\bbearer\s+([A-Za-z0-9._~+/=-]{8,}))"));
    return pattern;
}

const QRegularExpression &skPattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(\bsk-[A-Za-z0-9_-]{12,})"));
    return pattern;
}

const QRegularExpression &jwtPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"(\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,})"));
    return pattern;
}

const QRegularExpression &privateKeyPattern() {
    static const QRegularExpression pattern(
        QStringLiteral(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)"));
    return pattern;
}

const QRegularExpression &awsPattern() {
    static const QRegularExpression pattern(QStringLiteral(R"(\bAKIA[0-9A-Z]{16}\b)"));
    return pattern;
}

}  // namespace

QString replacement() { return replacementText(); }

Result redact(const QString &text) {
    Result result;
    result.text = text;
    if (text.isEmpty()) {
        return result;
    }

    // 私钥块先处理：它跨行，后面几条都是单行规则。
    {
        auto match = privateKeyPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("private-key"));
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                replacementText());
            match = privateKeyPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = authorizationPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("authorization"));
            const QString head = match.captured(1) + match.captured(2);
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                head + replacementText());
            match = authorizationPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = fieldPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("credential-field"));
            const QString head = match.captured(1) + match.captured(2);
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                head + replacementText());
            match = fieldPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = bearerPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("bearer"));
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                QStringLiteral("Bearer ") + replacementText());
            match = bearerPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = skPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("sk-key"));
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                replacementText());
            match = skPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = jwtPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("jwt"));
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                replacementText());
            match = jwtPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    {
        auto match = awsPattern().match(result.text);
        while (match.hasMatch()) {
            note(&result, QStringLiteral("cloud-key"));
            result.text.replace(match.capturedStart(), match.capturedLength(),
                                replacementText());
            match = awsPattern().match(result.text, match.capturedStart() + 1);
        }
    }

    return result;
}

bool containsCredentials(const QString &text) { return redact(text).containsCredentials(); }

QString preview(const QString &text, int maxChars) {
    const Result result = redact(text);
    if (maxChars <= 0 || result.text.size() <= maxChars) {
        return result.text;
    }
    return result.text.left(maxChars) + QStringLiteral("\n…（已截断）");
}

}  // namespace AgentRedaction
