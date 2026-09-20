/**
 * @file LauncherSettings.hpp
 * @brief 启动器自己的持久化设置：上次启动的版本、版本名格式。
 * @author 0x7fffff
 * @date 2026-09-20
 * @note 和 GlobalJavaSettings 分开：那边是 Java 运行时的配置，这边是启动器界面的状态。
 *       两边都走 QSettings，键名各自带前缀（Java/ 与 Version/）。
 */
#pragma once

#include <QObject>
#include <QString>

#include "../manage/VersionDisplay.hpp"

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

private:
    QString m_lastLaunchedVersion;
    VersionNameOptions m_versionNameOptions;
};
