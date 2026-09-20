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
    // 「版本名格式」
    // ------------------------------------------------------------------
    // NOTE: 这一项**目前没有任何界面入口**——项目里还没有设置界面（ui/MainWindowUI.cpp
    // 里那个 QMenuBar 是空的），所以下面两个函数目前只有 MainWindow 在读写，值一律是
    // VersionNameOptions 的默认值（自定义名✓ 版本名✓ 加载器名✓ 加载器版本✗）。
    //
    // TODO（别忘了这东西）: 以后做设置界面时，把这四个开关接出来。既可以摆成四个勾选框，
    //   也可以摆成一个"预设档位"的下拉框、由它去反写这四个布尔——数据模型就是下面这个
    //   四个布尔的 struct，界面想怎么摆都行，别为了界面去改模型。归一化由
    //   normalizeVersionNameOptions() 负责，界面只要照它约束勾选即可。
    //   入口的自然落点是 ui/MainWindowUI.cpp 里那个空 QMenuBar。
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
    VersionNameOptions m_versionNameOptions;
    LauncherBehaviour m_launcherBehaviour = LauncherBehaviour::Unchanged;
    DownloadSource m_downloadSource = DownloadSource::Official;
};
