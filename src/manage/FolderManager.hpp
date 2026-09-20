/**
 * @file FolderManager.hpp
 * @brief 「游戏文件夹」列表：启动器用哪一份 .minecraft、以及用户还添加过哪些。
 * @author LoserKugua
 * @date 2026-04-10
 * @note 列表的来源是 LauncherSettings（落盘在 QSettings 的 Launcher/Folders），
 *       本类负责列表的**语义**：自带那份永远在第一项且删不掉、当前目录不在盘上时
 *       退回自带那份、路径归一化与去重。
 *
 * 这个类和 AppPaths 的分工：本类只说"当前应该用哪个根目录"，**不动 AppPaths**（那是
 * 全局状态，改它是有副作用的一步）。界面（MainWindow）拿到 currentFolderChanged()
 * 之后调 AppPaths::setMinecraftDir() 再重灌界面 —— 副作用留在控制层，本类可以单独测。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class LauncherSettings;

class FolderManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构建文件夹管理器，并从盘上把列表读进来。
     * @param settings 读写设置的对象。**不接管所有权**，必须比本对象活得久。
     * @param parent Parent QObject.
     *
     * 构造完立刻就可以问 folders() / currentFolder()，界面按它摆第一帧。
     */
    explicit FolderManager(LauncherSettings *settings, QObject *parent = nullptr);

    /**
     * @brief 启动器自带的那一份根目录（<exe 目录>/.minecraft）。
     * @return 归一化之后的绝对路径。
     *
     * 它永远在列表第一项、也删不掉：别的目录被挪走、U 盘被拔掉的时候，启动器总得有
     * 一个能落的根目录（AppPaths 的默认值也正是它）。
     */
    static QString defaultFolder();

    /**
     * @brief 判断某个路径是不是自带的那一份。
     * @param path 待判定的路径（内部会先归一化，所以写法不同也能认出来）。
     */
    static bool isDefaultFolder(const QString &path);

    /**
     * @brief 全部可选根目录。
     * @return 自带那份在最前，后面是用户添加的（按添加顺序）。
     */
    QStringList folders() const;

    /**
     * @brief 当前正在用的根目录。
     * @return 一定非空：设定的那一份不存在（U 盘没插、目录被挪走）时退回自带那份。
     */
    QString currentFolder() const;

    /**
     * @brief 添加一个根目录。
     * @param path 目录路径。
     * @return 路径是个存在的目录（或本来就等于自带那份）时返回 true。
     *         **已经在列表里时同样返回 true**（不重复追加）—— 返回值表达的是
     *         "这条路径现在在列表里"，不是"这次新加进去了"。
     */
    bool addFolder(const QString &path);

    /**
     * @brief 删掉一个用户添加的根目录。
     * @param path 目录路径。
     * @return 真删掉了返回 true；自带那份、以及不在列表里的路径返回 false。
     *
     * 删掉的正好是当前用的那一份时，当前目录**退回自带那份**（否则启动器会指向一个
     * 已经不在列表里的目录）。
     */
    bool removeFolder(const QString &path);

    /**
     * @brief 换一个当前根目录。
     * @param path 目录路径，必须在 folders() 里、而且是个存在的目录。
     * @return 换成功了返回 true；不在列表里或目录不存在时返回 false 且什么都不做。
     *
     * 换成当前已经在用的那一份时也返回 true，但**不发信号**（没必要让界面重灌一遍）。
     */
    bool setCurrentFolder(const QString &path);

signals:
    /// 列表本身变了（加/删）—— 界面据此重灌左边那张列表。
    void foldersChanged();
    /// 当前根目录变了 —— 界面据此切 AppPaths 并重灌所有跟路径有关的东西。
    void currentFolderChanged(const QString &path);

private:
    /// 把用户输入/盘上读来的路径归一化成可比对的形式：绝对路径 + cleanPath；
    /// 目录真的存在时再取一次 canonicalFilePath（顺带统一盘符大小写、解掉符号链接，
    /// 否则同一份目录会以两种写法同时进列表）。
    static QString normalized(const QString &path);

    LauncherSettings *m_settings;
    QStringList m_userFolders;  // 用户添加的那些（不含自带那份）
    QString m_current;          // 归一化后的绝对路径，一定非空
};
