#pragma once
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QImage>
#include <QStringList>

class ToolboxPage;
class OverlayHost;
class QThread;
class QTimer;
class ResumableDownload;

/// Runtime-only resources; never installs an instance or writes to an existing client JAR.
class ArmorTrimPreviewController final : public QObject {
public:
    /// @param overlay 问「确认下载」时开在内容区中间的那一块（全仓「不出现独立窗口」，
    /// 下载确认也走它）。
    ArmorTrimPreviewController(ToolboxPage *page, OverlayHost *overlay,
                               QObject *parent = nullptr);
    ~ArmorTrimPreviewController() override;
    void ensureLocal();
    void refresh();
    void shutdown();
    /// @param cape 当前账号的披风；空 = 没有披风（那格勾选置灰，预览里也不画）。
    void setPlayerSkin(const QImage &skin, const QImage &cape, bool slim);
private:
    void request(int action);
    void load(const QStringList &archives, bool verifyCache = false);
    void download();
    void status(const QString &message, bool busy = false);
    QString cachePath() const;
    QPointer<ToolboxPage> m_page;
    OverlayHost *m_overlay = nullptr;
    QPointer<QThread> m_worker;
    ResumableDownload *m_download = nullptr;
    QTimer *m_deadline = nullptr;
    QHash<QString,QImage> m_images;
    QImage m_playerSkin;
    QImage m_playerCape;
    bool m_slim=false;
    QString m_root, m_source;
    bool m_busy=false, m_cancelled=false, m_checked=false, m_timedOut=false;
};
