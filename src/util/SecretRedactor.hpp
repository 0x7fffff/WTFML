#pragma once
#include <QReadWriteLock>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QStringView>
#include <algorithm>

namespace SecretRedactor {
inline QString replacement() { return QStringLiteral("<redacted>"); }
struct Registry { QReadWriteLock lock; QStringList values; };
inline Registry &registry() {
    // The Qt log handler can run during static teardown. Nothing is persisted.
    static auto *state = new Registry;
    return *state;
}
inline void remember(const QString &secret) {
    if (secret.size() < 8) return; // Ignore short offline placeholders such as "0".
    auto &state = registry();
    QWriteLocker guard(&state.lock);
    if (!state.values.contains(secret)) {
        state.values.append(secret);
        std::sort(state.values.begin(), state.values.end(), [](const auto &a, const auto &b) { return a.size() > b.size(); });
    }
}
// Register only defined credential fields; ordinary profile names and identifiers stay readable.
// Account lists contain nested token objects, so visit objects/arrays as well.
inline void rememberCredentialFields(const QJsonValue &value) {
    if (value.isArray()) {
        for (const auto &entry : value.toArray()) rememberCredentialFields(entry);
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString key = it.key().toLower();
            if (key == QLatin1String("access_token") || key == QLatin1String("refresh_token") ||
                key == QLatin1String("id_token") || key == QLatin1String("client_secret") ||
                key == QLatin1String("token")) remember(it.value().toString());
            if (it.value().isObject() || it.value().isArray()) rememberCredentialFields(it.value());
        }
    }
}
inline QStringList secrets() {
    auto &state = registry();
    QReadLocker guard(&state.lock);
    return state.values;
}
inline QString redact(QString text) {
    for (const auto &secret : secrets()) text.replace(secret, replacement());
    return text;
}
inline void rememberArguments(const QStringList &arguments) {
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const auto &arg = arguments.at(i);
        if (arg == QLatin1String("--accessToken") && i + 1 < arguments.size()) remember(arguments.at(i+1));
        for (const auto &prefix : {QStringLiteral("-Dminecraft.accessToken="), QStringLiteral("--accessToken=")})
            if (arg.startsWith(prefix)) remember(arg.mid(prefix.size()));
    }
}

// One instance per decoded output channel. Only a possible credential prefix is
// delayed; all ordinary text/newlines pass immediately. Final fragments are masked.
class Stream {
public:
    QString push(const QString &text, bool final = false) {
        m_pending += text;
        const auto values = secrets();
        if (values.isEmpty()) { QString out; out.swap(m_pending); return out; }
        QString out;
        out.reserve(m_pending.size());
        qsizetype pos = 0;
        while (pos < m_pending.size()) {
            const auto tail = QStringView(m_pending).mid(pos);
            bool matched = false;
            for (const auto &secret : values) {
                if (tail.front() != secret.front()) continue;
                if (tail.startsWith(QStringView(secret))) {
                    out += replacement(); pos += secret.size(); matched = true; break;
                }
                if (tail.size() < secret.size() && QStringView(secret).startsWith(tail)) {
                    if (final) { out += replacement(); pos = m_pending.size(); }
                    m_pending = m_pending.mid(pos);
                    return out;
                }
            }
            if (!matched) out += m_pending.at(pos++);
        }
        m_pending.clear();
        return out;
    }
private:
    QString m_pending;
};
} // namespace SecretRedactor
