/**
 * @file AgentSettingsPage.hpp
 * @brief 智能体的“API 与模型设置”整页（OverlayHost::showPage 里开，不另开窗口）。
 *
 * Stage 1A 只有模型服务 + 隐私/外发说明 + 会话加密语义；MCP 一栏不显示。
 * 密钥：已保存时留空、占位文字说明；保存前不发任何网络请求。
 */

#pragma once

#include "AgentViewTypes.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class AgentSecretEdit;

class AgentSettingsPage : public QDialog {
    Q_OBJECT

public:
    explicit AgentSettingsPage(QWidget *parent = nullptr);

    void setProfile(const AgentView::ProviderDraft &profile, bool hasStoredKey,
                    const QString &storageBackend);
    AgentView::ProviderDraft profile() const;
    QString apiKey() const;
    bool clearKeyRequested() const { return m_clearKey; }
    void setStatus(const QString &text, bool error = false);
    void setTestResult(const QString &text, bool ok);
    void setPrivacyText(const QString &text);

signals:
    void saveRequested();
    void testRequested();
    void clearKeyRequestedSignal();
    void deleteProfileRequested();

private:
    void updateUrlPreview();

    QComboBox *m_provider = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QLineEdit *m_model = nullptr;
    QSpinBox *m_contextWindow = nullptr;
    QSpinBox *m_maxOutput = nullptr;
    QDoubleSpinBox *m_temperature = nullptr;
    QSpinBox *m_timeout = nullptr;
    QComboBox *m_deployment = nullptr;
    QCheckBox *m_localHttp = nullptr;
    QGroupBox *m_advanced = nullptr;
    QComboBox *m_authMode = nullptr;
    QCheckBox *m_stream = nullptr;
    QComboBox *m_maxTokensField = nullptr;
    QComboBox *m_systemRole = nullptr;
    QComboBox *m_reasoningEcho = nullptr;
    QCheckBox *m_sendTemperature = nullptr;
    QCheckBox *m_sendToolChoice = nullptr;
    AgentSecretEdit *m_secret = nullptr;
    QLabel *m_storage = nullptr;
    QLabel *m_urlPreview = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_testResult = nullptr;
    QLabel *m_privacy = nullptr;
    QPushButton *m_test = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_clearKeyButton = nullptr;
    QPushButton *m_deleteProfile = nullptr;
    QString m_profileId;
    bool m_clearKey = false;
};
