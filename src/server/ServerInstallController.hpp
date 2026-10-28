/**
 * @file ServerInstallController.hpp
 * @brief 真的把一份服务端安装计划跑出来。
 *
 * 【它干的那几件事，和"怎么判断"分在两层】
 *
 * 该不该做、要取哪一份元数据、取回来怎么读、跑什么命令、分成几步 —— 全在
 * `ServerInstallRun.hpp`（纯函数，`verify` 直接钉）。这一个文件只管**接口那半边**：
 *
 * 取元数据 → 下文件 → 跑安装器 → 核对装好了没有
 *
 * 每一段都要碰外面的世界（网络 / 磁盘 / 进程），所以它是个 QObject，并且**只往前推**：
 * 每一段收尾时才决定要不要进下一段，任何一段失败就 `finished(Failed, 那句人话)` 收场。
 *
 * 【它为什么不自己 new 一个 DownloadManager】
 *
 * 下载那一层（`DownloadManager`）只有**一份批次状态**：镜像降级、按主机的并发守卫、
 * 看门狗、失败清单全在它身上。再 new 一个出来 = 同一时间有两条路在改同一批文件、
 * 两套"忙不忙"的说法。所以这一个拿的是**共用的那一份**（`setDownloadManager()`），
 * 而且起手先看它忙不忙（忙就拒掉 —— 先单任务聚合）。
 *
 * NOTE: 元数据那几次 GET 用的是**自己**的 `QNetworkAccessManager`，和 VersionManager
 * 给加载器元数据开的那一份是同一个道理（见那边 `m_loaderVersionRequests` 的注释）：
 * 那些是"问一句就完事"的请求，混进下载器的回复表里只会让两边的收尾互相算错。
 *
 * 【取消】
 *
 * `cancel()` 之后：没发出去的请求不发了、在飞的回复 abort、安装器进程杀掉。**盘上留下的
 * 半成品不删**（下一次「修复」接着用），但那一行**永远停在「未安装」** —— "装好了没有"
 * 的唯一判据是 `ServerModel::isInstalled()`（计划里那几条相对路径都在不在），不是"最后一个
 * 进程的退出码是 0"。这就是"取消和失败不会留下被当成成品的半文件"的落点。
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>

#include <functional>

#include "download/ServerInstall.hpp"

class DownloadManager;
class QNetworkReply;
class QProcess;
class QTimer;

namespace ServerInstallRun {

/// 这一趟的三个结局。**只有三个**：跑完了、失败了、用户取消了。
enum class Outcome {
    Succeeded,
    Failed,
    Cancelled,
};

class ServerInstallController : public QObject {
    Q_OBJECT

public:
    explicit ServerInstallController(QObject *parent = nullptr);

    /// 共用的那一份下载器（`VersionManager` 手上那个）。**必须给**，不给就装不了。
    void setDownloadManager(DownloadManager *downloads) { m_downloads = downloads; }

    /**
     * @brief 原版那一档的版本 json 从哪儿来。
     *
     * 装服务端的人多半已经把客户端那个版本装过了 —— 盘上 `versions/<版本>/<版本>.json` 里
     * 就有 `downloads.server`（就是原版服务端的地址）。给这一发好处是原版那一档**一个网络
     * 请求都不用发**。回空 json = 本地没有，控制器自己去问（见 `fetchVersionJson()`）。
     */
    void setLocalVersionJson(std::function<QJsonObject(const QString &)> source) {
        m_localVersionJson = std::move(source);
    }

    bool isRunning() const { return m_running; }

    /**
     * @brief 共用下载器上**开着的那一批**是不是这一趟开的（还在下的那一段）。
     *
     * “正在装服务端”和“**正占着下载器**”是两件事：中间跑安装器那一段可能要好几分钟，
     * 那时候这一趟一条网络作业都没有，别人开一批下载完全没问题。`cancel()` 与各处忙态
     * 闸门问的是这后一件事 —— 只问 `isRunning()` 会把安装器那几分钟也算成“下载器忙”。
     */
    bool ownsDownloadBatch() const { return m_batchOwned; }
    QJsonObject downloadContinuation() const;
    bool continueDownload(const QJsonObject &intent);
    // Acceptance tools can shorten deadlines without replacing the production process path.
    void setInstallerWatchdogLimits(int silentMs, int totalMs);

    /**
     * @brief 开装。
     *
     * @param serverId `Servers/<这个名字>` 那个目录（**调用方得先建好它和那一份记账** ——
     * 这样"排在任务中心那一项"和"列表里那一行"从第一秒起就是指同一个东西）。
     * @param request 用户在「新建服务端…」那一页上填的东西。
     * @param javaPath 跑安装器用哪个 java（那一档不跑安装器时用不上）。空 = 那一档要跑安装器
     * 但没给 java ⇒ 直接失败，措辞见 `javaMissingMessage()`。
     */
    void start(const ServerInstall::Request &request, const QString &serverId,
               const QString &javaPath, const ServerInstall::Plan *resolved = nullptr);

    /// 用户按了「取消」。
    void cancel();

signals:
    /// 任务中心那几步（**会发两次**：起手一份、元数据取回来之后一份更完整的）。
    void stepsChanged(const QStringList &steps, int runningIndex);
    /// 某一步跑完了 / 栽了（带一句写在那一行上的小字，可空）。
    void stepChanged(int index, bool ok, const QString &note);
    /// 当前那个文件的字节进度（`total <= 0` = 上游没报总量，界面别画百分比）。
    void bytesChanged(qint64 done, qint64 total);
    /// 写进启动器日志的几句话。
    void message(const QString &text);
    /// 收场。`errorSummary` 只在 `Failed` 时有意义。
    void finished(ServerInstallRun::Outcome outcome, const QString &errorSummary);

private:
    void fail(const QString &problem);
    void finishWith(ServerInstallRun::Outcome outcome, const QString &errorSummary);

    /// 第一段：把"还差的那一份元数据"拿到手（本地能拿到的就用本地那份）。
    void beginResolve();
    /// `Need::kind` 对应的那份字节到手了（`payload` 空 = 没拿到）。
    void onMetadataReady(const QByteArray &payload, const QString &problem);
    /// 从版本清单里找这个版本 json 的地址，然后取它（原版那一档本地没有时走这条）。
    void fetchVersionJson();
    void onVersionManifest(QNetworkReply *reply);

    /// 第二段：按计划把文件都下下来。
    void beginDownloads();
    void onDownloadFinished(const QString &fileName, bool success, const QString &error);

    /// 第三段：跑安装器（不跑的那几档直接跳到核对）。
    void beginInstaller();
    void onInstallerOutput();
    void onInstallerFinished(int exitCode);

    /// 第四段：核对装好了没有（判据是 `ServerRegistry::describe()` 里那一段）。
    void verifyInstall();

    DownloadManager *m_downloads = nullptr;
    std::function<QJsonObject(const QString &)> m_localVersionJson;
    QNetworkAccessManager *m_network = nullptr;
    QProcess *m_process = nullptr;
    QTimer *m_installerWatchdog = nullptr;
    QElapsedTimer m_installerProgress, m_installerTotal;
    int m_installerRetries = 0;
    bool m_installerRestartPending = false;
    int m_installerSilentMs = 180000, m_installerTotalMs = 600000;
    QNetworkReply *m_metadataReply = nullptr;

    bool m_running = false;
    bool m_cancelling = false;
    /// 共用下载器上开着的这一批是**这一趟**开的（见 `ownsDownloadBatch()`）。
    bool m_batchOwned = false;

    ServerInstall::Request m_request;
    ServerInstall::Plan m_plan;
    QString m_serverId;
    QString m_dir;
    QString m_javaPath;
    QJsonObject m_downloadOriginals;

    /// 计划里那些文件下完了几个、失败的那个叫什么（空 = 没失败）。
    int m_downloaded = 0;
    QString m_failedDownload;
    QString m_failedDownloadReason;
    /// 这一趟要下的那几个文件叫什么。下载器是**共用**的，别人下的文件也会从
    /// `downloadFinished()` 那儿过 —— 认名字才不会把别人的收尾算成自己的。
    QSet<QString> m_downloadNames;

    /// 跑安装器那一段攒下来的输出尾巴（失败时并进那句人话里）。
    QString m_installerTail;

    /// 当前那一步在 `stepNames()` 里的下标（`stepChanged()` 用它）。
    int m_stepIndex = 0;
};

}  // namespace ServerInstallRun
