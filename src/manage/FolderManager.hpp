/**
 * @file FolderManager.hpp
 * @brief 为 Minecraft 根目录文件夹管理辅助提供声明。
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
     * @brief 构建文件夹管理器。
     * @param parent Parent QObject.
     */
    explicit FolderManager(QObject *parent = nullptr);

    /**
     * @brief 添加一个自定义 Minecraft 根目录文件夹。
     * @param path Folder path to add.
     * @return 若文件夹有效且被添加，则返回 true.
     */
    bool addFolder(const QString &path);

    /**
     * @brief 移除一个先前添加的文件夹。
     * @param path Folder path to remove.
     * @return 若文件夹被移除，则返回 true.
     */
    bool removeFolder(const QString &path);

    /**
     * @brief 显示配置的文件夹列表。
     */
    QStringList folders() const;

private:
    QStringList m_folders;
};
