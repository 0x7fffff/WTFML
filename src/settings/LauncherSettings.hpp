/**
 * @file LauncherSettings.hpp
 * @brief 启动器自己的持久化设置：上次启动的版本、版本名格式、启动后行为、下载源。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 和 GlobalJavaSettings 分开：那边是 Java 运行时的配置，这边是启动器界面的状态。
 *       两边都走 QSettings，键名各自带前缀（Java/ 、Version/ 、Launcher/ 、Download/）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "../manage/VersionDisplay.hpp"

/**
 * @brief 游戏起来之后，启动器自己怎么办。
 *
 * NOTE: 「关掉」这一档刻意用 hide() 而不是 close()：close() 是"关窗口"，
 *       启动器是最后一个窗口时那等于退出进程，而游戏进程是它的子进程 ——
 *       用户以为只是把启动器收起来，结果游戏跟着没了。
 */
enum class LauncherBehaviour {
    Unchanged,       ///< 不变：启动器保持原样
    Minimize,        ///< 最小化启动器
    Close,           ///< 收起启动器（游戏退出后不自动回来）
    CloseAndRestore, ///< 收起启动器，游戏退出后自动重新显示
};

/**
 * @brief 下载走哪个源。
 *
 * NOTE: 存的是**字符串**而不是枚举下标：以后加一档、或者两档调个顺序，
 *       用户存下来的旧值不会被解读成另一个源（下载源选错是要重下几个 GB 的）。
 */
enum class DownloadSource {
    Official, ///< Mojang 官方源
    Mirror,   ///< 镜像源（BMCLAPI）
};

class LauncherSettings : public QObject {
    Q_OBJECT

public:
    explicit LauncherSettings(QObject *parent = nullptr);

    /**
     * @brief 上次成功启动过的版本号。
     * @return 版本目录名；从没启动过就是空串。
     *
     * 启动时用它把"已安装版本"下拉框预选上，免得每次打开都停在没有选中项的状态、
     * 让用户以为一个版本都没装。
     */
    QString lastLaunchedVersion() const;

    /**
     * @brief 记住这次启动的版本号，并立刻落盘。
     * @param versionId 版本目录名（不是显示名）。
     */
    void setLastLaunchedVersion(const QString &versionId);

    // ------------------------------------------------------------------
    // 「游戏文件夹」（实例页左边那张列表）
    // ------------------------------------------------------------------
    // 存的是**用户自己添加**的根目录，不含启动器自带的 <exe 目录>/.minecraft ——
    // 那一份由 AppPaths::defaultMinecraftDir() 算出来，存下来反而会在启动器被挪到
    // 别处之后变成一个指向旧地方的死路径。
    // 「自带那份永远排第一、删不掉、当前目录不在盘上怎么办」这些语义在 FolderManager 里。
    // ------------------------------------------------------------------

    /// 用户添加过的根目录，按添加顺序。
    QStringList folders() const;

    /// 覆盖用户添加的根目录列表并立刻落盘。
    void setFolders(const QStringList &folders);

    /// 当前选中的根目录。空串 = 用启动器自带的那一份。
    QString currentFolder() const;

    /// 记下当前选中的根目录并立刻落盘。传空串 = 回到自带的那一份。
    void setCurrentFolder(const QString &dir);

    // ------------------------------------------------------------------
    // 「版本名格式」
    // ------------------------------------------------------------------
    // NOTE: 界面入口在设置页的「版本名格式」那一行 —— 四个勾选框
    // （versionNameCustomCheck / RealCheck / LoaderCheck / LoaderVersionCheck，
    // ui/MainWindowUI.cpp），改动经 MainWindow::onVersionNameOptionChanged() 归一化后落盘。
    //
    // 当初留的那条余地仍然有效：这四个布尔也可以改摆成一个"预设档位"的下拉框、由它去
    //   反写这四个布尔——数据模型就是下面这个 struct，界面想怎么摆都行，别为了界面去改模型。
    //   归一化由 normalizeVersionNameOptions() 负责，界面只要照它约束勾选即可。
    //   默认值见 VersionDisplay.hpp 里 VersionNameOptions 的成员初始值。
    // ------------------------------------------------------------------

    /**
     * @brief 当前的「版本名格式」开关，已归一化。
     */
    VersionNameOptions versionNameOptions() const;

    /**
     * @brief 设置「版本名格式」开关并立刻落盘。
     * @param options 原始开关；内部会先 normalizeVersionNameOptions()。
     */
    void setVersionNameOptions(const VersionNameOptions &options);

    /**
     * @brief 从 QSettings 读回全部设置。构造函数里已经调过一次。
     */
    void load();

    /**
     * @brief 把当前值写进 QSettings 并 sync()，立刻落盘。
     */
    void save() const;

    // ------------------------------------------------------------------
    // 启动游戏之后启动器怎么办（设置界面「启动后」那一项）
    // ------------------------------------------------------------------

    /// 当前档位；盘上没有这一项、或者存的是不认识的值时是 Unchanged。
    LauncherBehaviour launcherBehaviour() const { return m_launcherBehaviour; }

    /// 设置档位并立刻落盘。
    void setLauncherBehaviour(LauncherBehaviour behaviour);

    // ------------------------------------------------------------------
    // 下载源
    // ------------------------------------------------------------------

    /// 当前下载源；盘上没有这一项、或者存的是不认识的值时是 Official。
    DownloadSource downloadSource() const { return m_downloadSource; }

    /// 设置下载源并立刻落盘。
    void setDownloadSource(DownloadSource source);

    // ------------------------------------------------------------------
    // 枚举 <-> 存储值
    // ------------------------------------------------------------------
    // 两个方向都放在这一处，免得"写进去"和"读回来"各写一套字符串、
    // 慢慢就拼歪了（拼歪的后果是静默退回默认档，用户看不出来）。

    /// @return 存进 QSettings 的字符串。
    static QString behaviourKey(LauncherBehaviour behaviour);
    /// @return 认不出来的值一律当 Unchanged。
    static LauncherBehaviour behaviourFromKey(const QString &key);

    /// @return 存进 QSettings 的字符串。
    static QString downloadSourceKey(DownloadSource source);
    /// @return 认不出来的值一律当 Official。
    static DownloadSource downloadSourceFromKey(const QString &key);

private:
    QString m_lastLaunchedVersion;
    QStringList m_folders;
    QString m_currentFolder;
    VersionNameOptions m_versionNameOptions;
    LauncherBehaviour m_launcherBehaviour = LauncherBehaviour::Unchanged;
    DownloadSource m_downloadSource = DownloadSource::Official;
};
