#pragma once

#include <QObject>
#include <functional>

class OverlayHost;

class InstanceFileController final : public QObject {
    Q_OBJECT
public:
    struct Host { std::function<void(const QString &)> log; };
    InstanceFileController(OverlayHost *overlay, Host host, QObject *parent = nullptr);

    bool toggle(const QString &path, QString *problem = nullptr);
    bool remove(const QString &path);
    bool removeUnasked(const QString &path, QString *problem = nullptr);
    void toggleMany(const QStringList &paths, bool disable);
    void removeMany(const QStringList &paths);

private:
    void log(const QString &text) const;
    OverlayHost *m_overlay = nullptr;
    Host m_host;
};
