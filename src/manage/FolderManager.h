#pragma once

#include <QObject>
#include <QStringList>

class FolderManager : public QObject
{
    Q_OBJECT

public:
    explicit FolderManager(QObject *parent = nullptr);
    bool addFolder(const QString &path);
    bool removeFolder(const QString &path);
    QStringList folders() const;

private:
    QStringList m_folders;
};
