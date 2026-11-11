/**
 * @file AgentSettingsPage.cpp
 * @brief API/模型设置整页的控件与校验提示（见 .hpp）。
 */

#include "AgentSettingsPage.hpp"

#include "AgentSecretEdit.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QSpinBox>
#include <QVBoxLayout>

AgentSettingsPage::AgentSettingsPage(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("agentSettingsPage"));
    setWindowTitle(QStringLiteral("API 与模型设置"));
    setModal(true);
    resize(560, 680);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(8);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *form = new QFormLayout(content);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    m_provider = new QComboBox(content);
    m_provider->setObjectName(QStringLiteral("agentProviderCombo"));
    m_provider->addItem(QStringLiteral("OpenAI-compatible（基础兼容子集）"),
                        QStringLiteral("openai-compatible"));
    form->addRow(QStringLiteral("Provider"), m_provider);

    m_name = new QLineEdit(content);
    m_name->setObjectName(QStringLiteral("agentProfileNameEdit"));
    form->addRow(QStringLiteral("配置名"), m_name);

    m_baseUrl = new QLineEdit(content);
    m_baseUrl->setObjectName(QStringLiteral("agentBaseUrlEdit"));
    m_baseUrl->setPlaceholderText(QStringLiteral("例如 https://api.example.com/v1"));
    form->addRow(QStringLiteral("Base URL"), m_baseUrl);

    m_urlPreview = new QLabel(content);
    m_urlPreview->setObjectName(QStringLiteral("agentFinalUrlPreview"));
    m_urlPreview->setWordWrap(true);
    m_urlPreview->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(QString(), m_urlPreview);

    m_model = new QLineEdit(content);
    m_model->setObjectName(QStringLiteral("agentModelEdit"));
    m_model->setPlaceholderText(QStringLiteral("模型 ID，例如 deepseek-chat"));
    form->addRow(QStringLiteral("模型 ID"), m_model);

    m_contextWindow = new QSpinBox(content);
    m_contextWindow->setObjectName(QStringLiteral("agentContextWindowSpin"));
    m_contextWindow->setRange(1024, 10000000);
    m_contextWindow->setSingleStep(1024);
    m_contextWindow->setValue(32768);
    form->addRow(QStringLiteral("上下文窗口"), m_contextWindow);

    m_maxOutput = new QSpinBox(content);
    m_maxOutput->setObjectName(QStringLiteral("agentMaxOutputSpin"));
    m_maxOutput->setRange(1, 1000000);
    m_maxOutput->setValue(4096);
    form->addRow(QStringLiteral("最大输出"), m_maxOutput);

    m_temperature = new QDoubleSpinBox(content);
    m_temperature->setObjectName(QStringLiteral("agentTemperatureSpin"));
    m_temperature->setRange(0.0, 2.0);
    m_temperature->setSingleStep(0.1);
    m_temperature->setValue(0.7);
    form->addRow(QStringLiteral("温度"), m_temperature);

    m_timeout = new QSpinBox(content);
    m_timeout->setObjectName(QStringLiteral("agentTimeoutSpin"));
    m_timeout->setRange(5, 600);
    m_timeout->setValue(120);
    m_timeout->setSuffix(QStringLiteral(" 秒"));
    form->addRow(QStringLiteral("超时"), m_timeout);

    m_deployment = new QComboBox(content);
    m_deployment->setObjectName(QStringLiteral("agentDeploymentCombo"));
    m_deployment->addItem(QStringLiteral("云端（内容会发送给第三方模型服务）"), QStringLiteral("cloud"));
    m_deployment->addItem(QStringLiteral("本地（本机服务，不经过 WTFML 服务器）"), QStringLiteral("local"));
    form->addRow(QStringLiteral("部署类型"), m_deployment);

    m_localHttp = new QCheckBox(QStringLiteral("允许本地明文 HTTP（只对 127.0.0.1 / localhost / [::1]）"), content);
    m_localHttp->setObjectName(QStringLiteral("agentLocalHttpCheck"));
    form->addRow(QString(), m_localHttp);

    m_secret = new AgentSecretEdit(content);
    m_secret->lineEdit()->setObjectName(QStringLiteral("agentApiKeyEdit"));
    m_secret->revealButton()->setObjectName(QStringLiteral("agentApiKeyRevealButton"));
    form->addRow(QStringLiteral("API Key"), m_secret);

    m_storage = new QLabel(content);
    m_storage->setObjectName(QStringLiteral("agentStorageStatus"));
    m_storage->setWordWrap(true);
    m_storage->setForegroundRole(QPalette::PlaceholderText);
    form->addRow(QString(), m_storage);

    m_advanced = new QGroupBox(QStringLiteral("协议适配（高级）"), content);
    m_advanced->setObjectName(QStringLiteral("agentAdvancedGroup"));
    m_advanced->setCheckable(true);
    m_advanced->setChecked(false);
    auto *advancedForm = new QFormLayout(m_advanced);
    m_authMode = new QComboBox(m_advanced);
    m_authMode->setObjectName(QStringLiteral("agentAuthModeCombo"));
    m_authMode->addItem(QStringLiteral("Bearer"), QStringLiteral("bearer"));
    m_authMode->addItem(QStringLiteral("api-key 头"), QStringLiteral("api-key-header"));
    m_authMode->addItem(QStringLiteral("无认证（仅本机）"), QStringLiteral("none"));
    advancedForm->addRow(QStringLiteral("认证头"), m_authMode);
    m_stream = new QCheckBox(QStringLiteral("流式 SSE"), m_advanced);
    m_stream->setObjectName(QStringLiteral("agentStreamCheck"));
    m_stream->setChecked(true);
    advancedForm->addRow(QString(), m_stream);
    m_maxTokensField = new QComboBox(m_advanced);
    m_maxTokensField->setObjectName(QStringLiteral("agentMaxTokensFieldCombo"));
    m_maxTokensField->addItem(QStringLiteral("max_tokens"), QStringLiteral("max_tokens"));
    m_maxTokensField->addItem(QStringLiteral("max_completion_tokens"), QStringLiteral("max_completion_tokens"));
    advancedForm->addRow(QStringLiteral("输出上限字段"), m_maxTokensField);
    m_systemRole = new QComboBox(m_advanced);
    m_systemRole->setObjectName(QStringLiteral("agentSystemRoleCombo"));
    m_systemRole->addItem(QStringLiteral("system"), QStringLiteral("system"));
    m_systemRole->addItem(QStringLiteral("developer"), QStringLiteral("developer"));
    advancedForm->addRow(QStringLiteral("系统角色"), m_systemRole);
    m_reasoningEcho = new QComboBox(m_advanced);
    m_reasoningEcho->setObjectName(QStringLiteral("agentReasoningEchoCombo"));
    m_reasoningEcho->addItem(QStringLiteral("只显示（不回传）"), QStringLiteral("display-only"));
    m_reasoningEcho->addItem(QStringLiteral("回传给后续请求"), QStringLiteral("send-back"));
    advancedForm->addRow(QStringLiteral("reasoning"), m_reasoningEcho);
    m_sendTemperature = new QCheckBox(QStringLiteral("发送 temperature"), m_advanced);
    m_sendTemperature->setObjectName(QStringLiteral("agentSendTemperatureCheck"));
    m_sendTemperature->setChecked(true);
    advancedForm->addRow(QString(), m_sendTemperature);
    m_sendToolChoice = new QCheckBox(QStringLiteral("发送 tool_choice"), m_advanced);
    m_sendToolChoice->setObjectName(QStringLiteral("agentSendToolChoiceCheck"));
    m_sendToolChoice->setChecked(true);
    advancedForm->addRow(QString(), m_sendToolChoice);
    form->addRow(m_advanced);

    m_strict = new QCheckBox(QStringLiteral("严格模式：关闭所有自动读取，每次发送前都预览"), content);
    m_strict->setObjectName(QStringLiteral("agentEgressStrictCheck"));
    form->addRow(QString(), m_strict);

    m_privacy = new QLabel(content);
    m_privacy->setObjectName(QStringLiteral("agentPrivacyText"));
    m_privacy->setWordWrap(true);
    m_privacy->setText(QStringLiteral(
        "标准模式（Stage 1A）：只发送你输入的文字、系统提示和模型选择。"
        "不读取文件、日志、实例元数据或凭据；不启动 MCP。"
        "云端模型的保留/训练策略由服务商决定，WTFML 不做遥测，也不能替服务商承诺。"));
    form->addRow(QStringLiteral("隐私与外发"), m_privacy);

    m_testResult = new QLabel(content);
    m_testResult->setObjectName(QStringLiteral("agentTestResultLabel"));
    m_testResult->setWordWrap(true);
    m_testResult->hide();
    form->addRow(QString(), m_testResult);

    auto *buttons = new QHBoxLayout;
    m_test = new QPushButton(QStringLiteral("测试连接"), this);
    m_test->setObjectName(QStringLiteral("agentTestConnectionButton"));
    m_clearKeyButton = new QPushButton(QStringLiteral("清除 API Key"), this);
    m_clearKeyButton->setObjectName(QStringLiteral("agentClearKeyButton"));
    m_deleteProfile = new QPushButton(QStringLiteral("删除此配置"), this);
    m_deleteProfile->setObjectName(QStringLiteral("agentDeleteProfileButton"));
    m_save = new QPushButton(QStringLiteral("保存"), this);
    m_save->setObjectName(QStringLiteral("agentSaveButton"));
    m_save->setDefault(true);
    buttons->addWidget(m_test);
    buttons->addWidget(m_clearKeyButton);
    buttons->addWidget(m_deleteProfile);
    buttons->addStretch(1);
    buttons->addWidget(m_save);
    root->addLayout(buttons);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("agentSettingsStatus"));
    m_status->setWordWrap(true);
    m_status->hide();
    root->addWidget(m_status);

    connect(m_baseUrl, &QLineEdit::textChanged, this, [this] { updateUrlPreview(); });
    connect(m_save, &QPushButton::clicked, this, &AgentSettingsPage::saveRequested);
    connect(m_test, &QPushButton::clicked, this, &AgentSettingsPage::testRequested);
    connect(m_clearKeyButton, &QPushButton::clicked, this, [this] {
        m_clearKey = true;
        m_secret->clear();
        m_secret->setStoredKey(false);
        setStatus(QStringLiteral("点了保存后才会真正清除已存凭据。"));
        emit clearKeyRequestedSignal();
    });
    connect(m_secret, &AgentSecretEdit::textChanged, this, [this] {
        if (!m_secret->text().isEmpty()) m_clearKey = false;
    });
    connect(m_deleteProfile, &QPushButton::clicked, this, &AgentSettingsPage::deleteProfileRequested);
    connect(m_advanced, &QGroupBox::toggled, this, [this](bool on) {
        for (QWidget *widget : m_advanced->findChildren<QWidget *>()) {
            if (widget->parentWidget() == m_advanced && widget != m_advanced) {
                widget->setVisible(on);
            }
        }
    });
    // QGroupBox 的 checkable 只负责禁用子控件，不负责藏起来；这里按“默认折叠”的语义
    // 主动隐藏高级项，之后由 toggled 统一接管。
    for (QWidget *widget : m_advanced->findChildren<QWidget *>()) {
        if (widget->parentWidget() == m_advanced && widget != m_advanced) {
            widget->hide();
        }
    }
    m_secret->setStoredKey(false);
    updateUrlPreview();
}

void AgentSettingsPage::updateUrlPreview() {
    QString base = m_baseUrl->text().trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    m_urlPreview->setText(base.isEmpty()
                              ? QStringLiteral("最终请求：<Base URL>/chat/completions")
                              : QStringLiteral("最终请求：%1/chat/completions").arg(base));
}

void AgentSettingsPage::setProfile(const AgentView::ProviderDraft &profile, bool hasStoredKey,
                                   const QString &storageBackend) {
    m_profileId = profile.id;
    m_name->setText(profile.name);
    m_baseUrl->setText(profile.baseUrl);
    m_model->setText(profile.model);
    m_contextWindow->setValue(profile.contextWindow);
    m_maxOutput->setValue(profile.maxOutput);
    m_temperature->setValue(profile.temperature);
    m_timeout->setValue(profile.timeoutSec);
    m_deployment->setCurrentIndex(qMax(0, m_deployment->findData(profile.deployment)));
    m_localHttp->setChecked(profile.localHttpAllowed);
    m_authMode->setCurrentIndex(qMax(0, m_authMode->findData(profile.authMode)));
    m_stream->setChecked(profile.stream);
    m_maxTokensField->setCurrentIndex(qMax(0, m_maxTokensField->findData(profile.maxTokensField)));
    m_systemRole->setCurrentIndex(qMax(0, m_systemRole->findData(profile.systemRole)));
    m_reasoningEcho->setCurrentIndex(qMax(0, m_reasoningEcho->findData(profile.reasoningEcho)));
    m_sendTemperature->setChecked(profile.sendTemperature);
    m_sendToolChoice->setChecked(profile.sendToolChoice);
    m_secret->clear();
    m_secret->setStoredKey(hasStoredKey);
    m_clearKey = false;
    m_storage->setText(hasStoredKey
                           ? (storageBackend == QLatin1String("dpapi-fallback")
                                  ? QStringLiteral("已保存（DPAPI 后备，本机本账户）；不回显值。")
                                  : QStringLiteral("已保存（Windows 凭据管理器，本机本账户）；不回显值。"))
                           : QStringLiteral("尚未保存 API Key。"));
    updateUrlPreview();
}

AgentView::ProviderDraft AgentSettingsPage::profile() const {
    AgentView::ProviderDraft profile;
    profile.id = m_profileId.isEmpty() ? QStringLiteral("default") : m_profileId;
    profile.name = m_name->text().trimmed().isEmpty() ? QStringLiteral("默认模型服务")
                                                      : m_name->text().trimmed();
    profile.baseUrl = m_baseUrl->text().trimmed();
    profile.model = m_model->text().trimmed();
    profile.contextWindow = m_contextWindow->value();
    profile.maxOutput = m_maxOutput->value();
    profile.temperature = m_temperature->value();
    profile.timeoutSec = m_timeout->value();
    profile.deployment = m_deployment->currentData().toString();
    profile.localHttpAllowed = m_localHttp->isChecked();
    profile.authMode = m_authMode->currentData().toString();
    profile.stream = m_stream->isChecked();
    profile.maxTokensField = m_maxTokensField->currentData().toString();
    profile.systemRole = m_systemRole->currentData().toString();
    profile.reasoningEcho = m_reasoningEcho->currentData().toString();
    profile.sendTemperature = m_sendTemperature->isChecked();
    profile.sendToolChoice = m_sendToolChoice->isChecked();
    return profile;
}

QString AgentSettingsPage::apiKey() const { return m_secret->text().trimmed(); }

void AgentSettingsPage::setStatus(const QString &text, bool error) {
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #b3261e;") : QString());
    m_status->setVisible(!text.isEmpty());
}

void AgentSettingsPage::setTestResult(const QString &text, bool ok) {
    m_testResult->setText(text);
    m_testResult->setStyleSheet(ok ? QStringLiteral("color: #1b5e20;")
                                   : QStringLiteral("color: #b3261e;"));
    m_testResult->setVisible(!text.isEmpty());
}

void AgentSettingsPage::setPrivacyText(const QString &text) { m_privacy->setText(text); }

void AgentSettingsPage::setStrictEgress(bool strict) { m_strict->setChecked(strict); }
bool AgentSettingsPage::strictEgress() const { return m_strict->isChecked(); }
