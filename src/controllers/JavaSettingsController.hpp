#pragma once

#include <QMap>
#include <QObject>

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
};
