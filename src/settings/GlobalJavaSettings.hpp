#pragma once

#include <QObject>
#include <QString>

class GlobalJavaSettings : public QObject
{
    Q_OBJECT

public:
    explicit GlobalJavaSettings(QObject *parent = nullptr);

    QString javaPath() const;
    void setJavaPath(const QString &path);
    bool ensureJavaAvailable();
    bool ensureJavaForComponent(const QString &component);
    bool ensureJavaForVersion(int majorVersion);
    QMap<int, QString> scanLocalJavaVersions();
    bool downloadJavaFromMojang(int majorVersion);

    int xmx() const;
    void setXmx(int mbytes);

    int xms() const;
    void setXms(int mbytes);

    QString extraJvmArgs() const;
    void setExtraJvmArgs(const QString &args);

    void load();
    void save() const;

private:
    QString m_javaPath;
    int m_xmx;
    int m_xms;
    QString m_extraJvmArgs;
};
