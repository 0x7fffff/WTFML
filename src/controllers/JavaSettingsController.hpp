#pragma once

#include <QMap>
#include <QObject>
#include <QString>

#include <functional>

class GlobalJavaSettings;
class JavaManageDialog;
class JavaRuntimeController;
class MainWindowUI;
class QTimer;
class QWidget;

class JavaSettingsController final : public QObject {
    Q_OBJECT

public:
    struct Host {
        std::function<void(const QString &)> log;
        /// 可选扫描提供方，在工作线程执行；必须只捕获可独立存活的值。
        std::function<QMap<int, QString>(const QString &)> scanJava = {};
    };

    JavaSettingsController(MainWindowUI *ui, GlobalJavaSettings *settings,
                           JavaRuntimeController *runtime, QWidget *dialogParent, Host host,
                           QObject *parent = nullptr);

    void openManagePage();
    void resetDiscovery();
    void refreshList();
    void refreshMemory();
    void updatePathLabel();
    void useAuto();
    void useRuntime(int major);
    /// 把全局 Java 手动钉到**任意一条** java.exe 上（不必在扫描结果里）。
    ///
    /// 先在工作线程认主版本（`release` 文件走快路径，读不到才真跑一次 `java -version`，
    /// 最多 5 秒），认出来才写设置；认不出来弹一句、一个字节都不写。
    /// 公开是因为 mw_check 直接驱动它 —— 选文件那个模态框测试驱动不了。
    void setManualJava(const QString &path);
    void detect();
    void download(int major);
    void downloadAllMissing();
    void setWarnMismatchFromUi();
    void commitMemoryFromSpin();
    void syncMemorySlider();
    void setMemoryModeFromUi();
    void applyMemoryLimits(qint64 totalMb);
    bool clampXmsToXmx(int xmxMb);
    void applyDetected(const QMap<int, QString> &versions);
    bool discoveryPending() const { return m_discoveryPending; }

signals:
    void discoveryFinished();

private:
    /// 「手动指定 java.exe…」那一颗：开系统选文件框，选完交给 `setManualJava()`。
    void browseManualJava();
    /// 这一行**画的是**哪条路径（扫描结果，或者手动钉的那条盖在上面）。行里画的和
    /// 「选用」点下去要钉的必须是同一条 —— 分成两处各算一遍就会对不上。
    QString rowPathFor(int major) const;
    void queueDiscovery(std::function<QMap<int, QString>()> scan);
    void pushRows();
    void log(const QString &message) const;
    bool memoryPageVisible() const;

    MainWindowUI *m_ui = nullptr;
    GlobalJavaSettings *m_settings = nullptr;
    JavaRuntimeController *m_runtime = nullptr;
    QWidget *m_dialogParent = nullptr;
    Host m_host;
    JavaManageDialog *m_page = nullptr;
    QTimer *m_memoryRefreshTimer = nullptr;
    bool m_scanned = false;
    bool m_discoveryPending = false;
    quint64 m_discoveryGeneration = 0;
    QMap<int, QString> m_versions;
    /// 用户手动挑的那一份（`setManualJava()` 认成功之后才有值）。只在"它仍是盘上钉的那条
    /// 路径"时参与画行 —— 启动/外面改了设置，这一格自然失效，不当成另一种真相。
    QString m_manualPath;
    int m_manualMajor = 0;
    /// 连点两次「手动指定」时，先前那次探测的结果作废（新的一次说了算）。
    quint64 m_manualGeneration = 0;
};
