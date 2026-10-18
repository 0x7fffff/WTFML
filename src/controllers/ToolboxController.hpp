/**
 * @file ToolboxController.hpp
 * @brief 工具箱那几件小工具的脑子（页面在 `ui/ToolboxPage`，纯算术在 `src/tools/`；
 *        「高速下载」不是纯算术，它自己一份 `DirectDownloadController`）。
 * @author 0x7fffff
 * @date 2026-10-16
 * @note 用户 2026-10-16 原话：「不要再往 mainwindow 里堆超级屎山了，用 controller 等等方式
 *       合理分散功能」。所以这一件**一行逻辑都不往 MainWindow 放**：那边只有三件事 ——
 *       建它、把底栏面板那六颗按钮接到 `open()` 上、析构时 `shutdown()`。
 *
 * 【这个 controller 管什么、不管什么】
 *
 * 管：
 *   - **哈希**（`FileHash`）：一个可能几百 MB 的文件要**一条线程**去算，进度和取消都在这儿；
 *   - **服务器在线检测**（`ServerPing::Pinger`）：异步那一套本来就在 `ServerPing` 里
 *     （服务器列表「测速」用的是同一个），这里只是拿它问一台；
 *   - **另三件**（附魔顺序、文本生成器、坐标换算）：当场就能算的纯函数，这里只做
 *     "把页面上的控件读成参数 → 调 `src/tools/` → 把结果翻成页面认的那几个字符串"。
 *
 * 不管：任何"往盘上写"的事（这一层是只读的 —— 工具箱里没有一件会改游戏文件；
 *       「高速下载」只写用户自己指定的目标文件，那条链在 `DirectDownloadController`）。
 *
 * 【为什么那几张表由 controller 喂给页面】
 *
 * `ui/` 不 include `src/`（全仓的老规矩）。所以"十六色是哪十六个""算法有哪三档""维度有
 * 哪几个""附魔表长什么样"这四张表都由这里读出来、翻成**字符串**再交给页面 —— 表本身
 * 各自只有一处出处（`TextFormat` / `FileHash` / `CoordConvert` / `Anvil`）。
 */

#pragma once

#include <QObject>
#include <QString>

#include <QAtomicInt>
#include <QHash>
#include <QImage>
#include <QStringList>

#include "tools/FileHash.hpp"

#include <functional>

class OverlayHost;
class QFileSystemWatcher;
class QThread;
class QWidget;
class ToolboxPage;
class DirectDownloadController;
class ActivityCenterController;

namespace ServerPing {
class Pinger;
}

class ToolboxController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        /// 写一行日志（这一层自己只记"问了什么、算出了什么"这种一行）。
        std::function<void(const QString &)> log;
        /// 「覆盖层被占着」时静默收掉再继续（`MainWindow::runWhenOverlayFree()` —— 唯一出处）。
        std::function<void(std::function<void()>)> openWhenOverlayFree;
        ActivityCenterController *activity = nullptr;
    };

    ToolboxController(OverlayHost *overlay, QWidget *pageParent, Host host,
                      QObject *parent = nullptr);
    ~ToolboxController() override;

    /**
     * @brief 打开工具箱那一页，并切到 @p toolKey 那一件（`ToolboxPage::toolKey()` 认人）。
     *
     * 认不出来的键（不该发生）什么都不做。覆盖层上正压着别的页时**静默收掉再开**
     * （走 `Host::openWhenOverlayFree`，和全仓"再开一页不再问"那一条同一个路数）。
     */
    void open(const QString &toolKey);

    /// 收线程（析构会调一次；`MainWindow` 收尾时也调一次）。
    void shutdown();
    bool ownsDownloadTask(quint64 id) const;
    bool canCancelDownloadTask(quint64 id) const;
    bool cancelDownloadTask(quint64 id);

private:
    QHash<QString, QImage> m_bannerTextures;
    bool m_bannerTextureLoading = false;
    // ---------- 附魔顺序助手 ----------
    void runAnvilPlan();
    // ---------- 文本生成器 ----------
    void refreshText();
    void loadTextFromJson(const QString &json);
    // ---------- 坐标换算 ----------
    void convertCoordinates(int dimensionIndex, double x, double y, double z);
    void parseTeleport(const QString &text);
    void makeTeleportCommand(int dimensionIndex, double x, double y, double z);
    // ---------- 服务器在线检测 ----------
    void checkServer(const QString &address);
    void cancelServerCheck();
    // ---------- 文件哈希校验 ----------
    void runHash(const QString &path, int algorithmIndex, const QString &expected);
    /// 批量算（§9.11 第 1 项「补多文件」）：一个线程里逐个算，中途可以取消。
    void runHashBatch(const QStringList &paths, int algorithmIndex);
    /// 把批量清单写到用户挑的那个路径（写盘只在这一层做）。
    void exportHashBatch(const QString &path, const QString &text);
    void cancelHash();
    /// 算完之后盯着的那个文件被改过 / 被删了（`QFileSystemWatcher` 那一发）。
    void onHashFileChanged(const QString &path);
    // ---------- 信标计算 / 酿造助手 ----------
    void refreshBeacon(int fromLevel, int level, int materialIndex, int beaconCount);
    void refreshBrewing(int potionIndex, bool extended, bool level2, int formIndex, int bottles);
    // ---------- 旗帜设计 / 纹饰搭配 ----------
    void refreshBanner(int baseColor, const QStringList &layers);
    void refreshTrim(int armorMaterial, int part, int patternIndex, int trimMaterial, int pieces,
                     bool wholeSet);

    /// 那一页认的那几个维度（`setDimensionChoices` 喂的就是它）。**下标 = 页面上下拉框的
    /// 下标**，所以"第 i 项是哪个维度"只有这一处说得出口。
    QStringList dimensionNames() const;
    /// 第 @p index 项对应的维度（越界就回"认不出来"那一档）。
    int dimensionAt(int index) const;

    OverlayHost *m_overlay = nullptr;
    QWidget *m_pageParent = nullptr;
    Host m_host;
    ToolboxPage *m_page = nullptr;
    DirectDownloadController *m_directDownload = nullptr;
    ServerPing::Pinger *m_pinger = nullptr;

    /// 哈希那一趟的线程（`QThread::create` 那种，和 `IntegrityController` 同一套做法）。
    QThread *m_hashThread = nullptr;
    /// 用户点了「停下」置 1（工作线程每一块读之前看一眼）。
    QAtomicInt m_hashAbort{0};
    /// 算完之后盯着的那个文件（"文件变化使结果失效"，§9.11 第 1 项）。
    QFileSystemWatcher *m_hashWatcher = nullptr;
    QString m_hashWatchedPath;
    qint64 m_hashWatchedBytes = -1;
    qint64 m_hashWatchedModifiedMs = -1;
    bool m_hashWatched = false;
    /// 批量那一趟算成功的那些文件（路径 → 算完那一刻的长相）。哪一条对不上了就从这里摘掉
    /// 并提醒一次（`onHashFileChanged()`）。
    QHash<QString, FileHash::Stamp> m_hashBatchStamps;
    /// 这一趟问的是哪个地址（`Pinger::oneFinished` 只给下标、不给地址，所以自己记着）。
    QString m_serverAddress;
};
