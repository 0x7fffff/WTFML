/**
 * @file FolderManager.hpp
 * @brief Declares folder management helpers for custom Minecraft roots.
 * @author LoserKugua
 * @date 2026-04-10
 * @note 管理自定义 Minecraft 根目录列表，避免使用全局变量。
 */
#pragma once

#include <QObject>
#include <QStringList>

class FolderManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Constructs the folder manager.
     * @param parent Parent QObject.
     */
    explicit FolderManager(QObject *parent = nullptr);

    /**
     * @brief Adds a custom Minecraft root folder.
     * @param path Folder path to add.
     * @return True when the folder is valid and added.
     */
    bool addFolder(const QString &path);

    /**
     * @brief Removes a previously added folder.
     * @param path Folder path to remove.
     * @return True if the folder was removed.
     */
    bool removeFolder(const QString &path);

    /**
     * @brief Returns the list of configured folders.
     */
    QStringList folders() const;

private:
    QStringList m_folders;
};
