/**
 * @file DownloadCompletionController.hpp
 * @brief 下载批次**收尾那一刻**该说什么、该应用什么（原来摊在
 *        `MainWindow::onVersionDownloadFinished()` 与它旁边那六格成员里）。
 *
 * 【为什么单独一层】
 *
 * 公共终态信号 `VersionManager::versionDownloadFinished(id, ok, msg)` 是**一条信号、
 * 六种来路**：普通版本下载、装一个带名字的实例、导入整合包、「补全文件」、GTNH 首启资源
 * 预取，以及"装完之后再把预设落上去"那一趟。分辨它们靠的是主窗口上那几个"这一次是替谁
 * 做的"标记 —— 而这一层持有的正是**"这一次安装排上了哪几份预设"**那一套：
 *
 *  * `m_pendingPresets` —— 点「安装」那一刻定死的 (预设 id, 是否已确认「覆盖」)；
 *  * `m_pendingInstance` —— 这一趟装的是哪个实例；
 *  * `m_fingerprints` —— 确认那一刻的预设指纹（收尾时要核，变了就不许拿旧确认去覆盖）；
 *  * `m_modsReady/modsOk/modsReport` —— 「先把模组和必要前置下下来」那一步的进度与结论。
 *
 * 【为什么它不能留在安装页那一层】
 *
 * 这一套**不是**"用户在页面上挑了哪几份"。用户从点下「安装」到收尾之间还可能回去改弹窗
 * 里的选择，那一动不该改变这一趟已经发出去的安装。所以写它的是三处（安装按钮、
 * `DownloadInstallContinuation` 断点续装、`InstanceUpdateFlow` 的升级那一趟），读它的是
 * 收尾这一发 —— 安装页只负责把选择交过来。
 *
 * 【这一层一条判据都不自己写】
 *
 * 该装哪几份、指纹对不对、应用之后说什么，全在 `PresetController`；任务账在
 * `ActivityCenterController`；"能启动的实例"那张表在 `installingVersionIds()`。
 */

#pragma once

#include <QMap>
#include <QObject>
#include <QPair>
#include <QList>
#include <QString>

class MainWindow;

class DownloadCompletionController final : public QObject {
public:
    explicit DownloadCompletionController(MainWindow *owner);

    /// 公共终态（接 `VersionManager::versionDownloadFinished`）。分成六种来路说话，
    /// 中间还夹着"装完之后把预设落上去"那一步（本层状态唯一的使用者）。
    void onFinished(const QString &versionId, bool success, const QString &errorMessage);

    /// 这一趟安装排上了什么（点「安装」/ 断点续装 / 升级那一趟各写一次）。
    void begin(const QString &instanceId, const QList<QPair<QString, bool>> &presets,
               const QMap<QString, QString> &fingerprints);
    /// 什么都没排上（下一次安装不该继承这一次的）。
    void clear();

    /// 断点续装要读回"这一趟排上了哪几份"（写进续装的意图里）。
    const QList<QPair<QString, bool>> &presets() const { return m_pendingPresets; }
    const QMap<QString, QString> &fingerprints() const { return m_fingerprints; }

private:
    MainWindow &m_owner;
    QList<QPair<QString, bool>> m_pendingPresets;
    QString m_pendingInstance;
    QMap<QString, QString> m_fingerprints;
    bool m_modsReady = false;
    bool m_modsOk = true;
    QString m_modsReport;
};
