#pragma once
#include <QHash>
#include <QObject>
#include "download/ResourceCatalog.hpp"
#include "manage/ModpackImport.hpp"
class MainWindow;
class QWidget;
class ImportController final : public QObject {
    Q_OBJECT
public:
    explicit ImportController(MainWindow *owner);
    void onImportModpackClicked();
    void importModpackFromFile(const QString &zipPath, const QString &namePrefill);
    bool applyModpackImport(const QString &zipPath, const QString &instanceName, const QByteArray &manifestJson, ModpackImport::Format format);
    bool installLocalResourceFile(const QString &versionId, ResourceCatalog::Kind kind, const QString &path, QString *problemOut);
    void handleInstanceSettingsDrop(QWidget *page, const QString &versionId, const QStringList &paths);
    void importDroppedModpack(const QString &zipPath, const QString &namePrefill);
    void handleWindowDrop(const QStringList &paths);
private:
    void repairFromReport(const QString &reportPath);
    /// 「已经解析好的包」那半截：加载器检查、CurseForge 换地址、重名复查、挑安装器 Java、
    /// 置忙态、交给数据层。界面上那一半（问名字、确认框）和验证工程那一半都落到这里。
    bool applyResolvedModpackImport(const QString &zipPath, const QString &instanceName,
                                    ModpackImport::Pack pack,
                                    const QString &manifestFingerprint);
    MainWindow *m_owner = nullptr;
    /// 一条导入流程正在走（从读清单到 `applyModpackImport` 返回）。
    ///
    /// NOTE: 这不是"在下载"（`m_downloadInFlight`），而是"还在问名字/换清单"那一小段：
    /// CurseForge 预览现在已经让出 UI 线程（嵌套事件循环），那期间「导入整合包…」
    /// 和拖放**仍然点得动**。没有这道闸的话第二趟会从问名字一路走到
    /// `m_importingInstanceId = B`，两趟同时往 versions 下面写。
    bool m_importFlow = false;
    /// PCL 那种"压缩包里套一份 .mrpack"解出来的临时包：实例名 → 临时文件。
    /// 导入真的开始了才登记（提前退出那些路当场就删），结束（`versionDownloadFinished`，
    /// 成功失败都算）时删掉 —— 数据层在整段导入里还要读它。
    QHash<QString, QString> m_tempArchives;
};

