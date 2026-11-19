#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

class OverlayHost;

class InstanceFileController final : public QObject {
    Q_OBJECT
public:
    struct Host {
        std::function<void(const QString &)> log;
        /// 这个路径现在能不能动：返回非空就是拒绝理由。
        ///
        /// 文件页单独一套控制器，不知道"这个实例正在跑 / 正在被复制 / 正在打快照"。
        /// 不接这一根线的话，备份正在读整棵目录树时用户还能把某个存档删掉，包里就会
        /// 少掉正被改的那几个文件，收尾却报"备份完成"。判据由宿主（MainWindow）给，
        /// 控制器只负责在改盘之前问一次。
        std::function<QString(const QString &)> refuseReason;
        /// 反推一个路径属于哪个实例。批量文件页操作要把它记进全局「文件操作中」集合，
        /// 否则备份 / 快照会在用户正删一个大目录时开始读同一棵树。
        std::function<QString(const QString &)> instanceIdOfPath;
        /// 标记 / 摘掉实例的文件操作占位。批量操作开始前后成对调。
        std::function<void(const QString &, bool)> setFileOperationBusy;
    };
    InstanceFileController(OverlayHost *overlay, Host host, QObject *parent = nullptr);

    bool toggle(const QString &path, QString *problem = nullptr);
    bool remove(const QString &path);
    bool removeUnasked(const QString &path, QString *problem = nullptr);
    void toggleMany(const QStringList &paths, bool disable);
    void removeMany(const QStringList &paths);

    enum class LifecycleAction { Copy, Move, Remove };
    struct LifecycleResult {
        qsizetype done = 0;
        QString problem;
    };

    /// @brief 对已获确认的批次先整体校验；执行时遇到首个失败即停止，不覆盖目标。
    /// @param nested 来源文件可以在 @p sourceRoot 的子目录里（结构页：各存档 generated 下的 nbt）。
    ///        复制 / 移动落到 @p destination 的一层，所选文件里有重名的整批拒绝。
    LifecycleResult applyLifecycleBatch(LifecycleAction action, const QString &sourceRoot,
                                        const QStringList &paths, const QString &destination = {},
                                        bool nested = false);

private:
    void log(const QString &text) const;
    /// 这一批路径里第一个拒绝理由（空 = 都可以动）。
    QString firstRefusal(const QStringList &paths) const;
    /// 把一批路径归到实例上并标记「文件操作中」；返回要释放的 id。
    ///
    /// 自己标记的占位不能让同一个控制器自己的逐项操作再拒绝一次，所以真正动盘的
    /// `toggleUnchecked()` / `removeUnchecked()` 不做宿主闸；闸只在公开入口和批量开头。
    QStringList beginFileOperation(const QStringList &paths);
    void endFileOperation(const QStringList &ids);
    bool toggleUnchecked(const QString &path, QString *problem);
    bool removeUnchecked(const QString &path, QString *problem);
    OverlayHost *m_overlay = nullptr;
    Host m_host;
};
