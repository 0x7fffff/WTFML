/**
 * @file InstanceUpgradePage.hpp
 * @brief 迁移式升级页（规范 U1）：来源 / 目标双栏、九类选择、底部摘要与确认。
 *
 * 【这一版只做"画状态 + 转发动作"】
 *
 * 判断全在 `operations/UpgradePageState`（目标/名字/报告有效期/能否确认）和
 * `operations/UpgradeSelection`（九类、默认值、合计）里，页面不另写一份判据：
 * `setState()` 把状态画上去，用户动作原样发信号出去，由调用方改状态再送回来。
 *
 * 这样页面可以在没有 `MainWindow` / 没有真下载的情况下单独建出来、单独验（`ui_check`
 * 的 UG 段），也避免"控件里藏一套规则、控制器里又有一套"。
 */
#pragma once

#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

#include "operations/UpgradePageState.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class InstanceUpgradePage final : public QWidget {
    Q_OBJECT

public:
    explicit InstanceUpgradePage(QWidget *parent = nullptr);

    /// 选项来源由调用方给（页面不自己去查版本列表）。只填下拉框，不改状态。
    void setVersionOptions(const QStringList &gameVersions);
    void setLoaderOptions(const QList<QPair<QString, QString>> &loaders, // (key, 展示名)
                          const QStringList &loaderVersions);

    /// 把状态画上去。不会发信号（避免"画状态"被当成用户动作）。
    void setState(const UpgradePageState::State &state);
    const UpgradePageState::State &state() const { return m_state; }

    /// 底部摘要那一行用：合计文件数与字节数（扫描完才有）。
    void setAmounts(int files, qint64 bytes);

    /// 每一类的文件数 / 字节数（只读计划扫出来之后才有；没给的类只显示说明文字）。
    void setCategoryAmounts(const QHash<QString, QPair<int, qint64>> &amounts);

    /// 每一类里有多少"链接"（升级复制不跟随链接；有就在那一行明说）。
    void setCategoryLinks(const QHash<QString, int> &links);

    /// 过渡期开关：九类选择 / 更新记录入口在执行侧（U5/U6）落地之前先不露出来，
    /// 免得"勾了却不生效"。默认显示。
    void setSelectionVisible(bool visible);
    void setHistoryVisible(bool visible);

    // --- 给验证工程按名字找控件用（生产代码不靠这些字符串） ---
    static QString sourceNameLabelName();
    static QString sourceFactsLabelName();
    static QString targetVersionName();
    static QString targetLoaderName();
    static QString targetLoaderVersionName();
    static QString instanceNameEditName();
    static QString categoryCheckName(const QString &categoryKey);
    static QString categoryNoteName(const QString &categoryKey);
    static QString summaryLabelName();
    static QString blockerLabelName();
    static QString reportButtonName();
    static QString historyButtonName();
    static QString previewButtonName();

signals:
    void targetVersionChanged(const QString &gameVersion);
    void targetLoaderChanged(const QString &loaderKey, const QString &loaderVersion);
    void instanceNameEdited(const QString &name);
    void categoryToggled(const QString &categoryKey, bool on);
    void reportRequested();
    void historyRequested();
    void previewRequested();

private:
    void rebuildSummary();

    UpgradePageState::State m_state;
    int m_amountFiles = 0;
    qint64 m_amountBytes = 0;

    QLabel *m_sourceName = nullptr;
    QLabel *m_sourceFacts = nullptr;
    QComboBox *m_targetVersion = nullptr;
    QComboBox *m_targetLoader = nullptr;
    QComboBox *m_targetLoaderVersion = nullptr;
    QLineEdit *m_instanceName = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_blocker = nullptr;
    QPushButton *m_report = nullptr;
    QPushButton *m_history = nullptr;
    QPushButton *m_preview = nullptr;
    QList<QCheckBox *> m_checks;
    QHash<QString, QPair<int, qint64>> m_categoryAmounts;
    QHash<QString, int> m_categoryLinks;
};





