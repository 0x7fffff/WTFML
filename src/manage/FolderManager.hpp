/**
 * @file FolderManager.hpp
 * @brief 为 Minecraft 根目录文件夹管理辅助提供声明。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 管理自定义 Minecraft 根目录列表，避免使用全局变量。
 *
 * NOTE: **这个类目前对产品行为是惰性的**，别被下面的 API 骗了：列表只存在内存里，
 *       既不落盘、也没人读出来 —— `folders()` 全项目没有任何调用点，`m_folders` 只在
 *       本类内部被增删。界面上那两个按钮（实例页的「添加文件夹」/「移除文件夹」）点完
 *       只写一行日志。真正要用起来得先定：这份列表存哪个键、以及启动器怎么在多个
 *       `.minecraft` 之间切换（`AppPaths` 现在把数据目录钉死在 exe 旁边那一处）。
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
     * @return 路径是个存在的目录就返回 true。**已经在列表里时同样返回 true**
     *         （只是不重复追加），返回值表达的是"这条路径现在在列表里"，不是"这次新加进去了"。
     */
    bool addFolder(const QString &path);

    /**
     * @brief 移除一个先前添加的文件夹。
     * @param path Folder path to remove.
     * @return 若文件夹被移除，则返回 true.
     */
    bool removeFolder(const QString &path);

    /**
     * @brief 取当前记着的文件夹列表。
     * @return 列表的副本。NOTE: **目前没有任何调用点**（见文件头的说明）。
     */
    QStringList folders() const;

private:
    QStringList m_folders;
};
