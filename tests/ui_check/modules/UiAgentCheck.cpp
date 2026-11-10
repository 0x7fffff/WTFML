/**
 * @file UiAgentCheck.cpp
 * @brief 智能体页/设置页的纯 UI 检查（offscreen）：五页导航、双栏几何、空态、消息渲染、
 *        API Key 遮挡、协议适配与隐私说明。
 */

#include "Support.hpp"

#include "AgentMarkdown.hpp"
#include "AgentComposer.hpp"
#include "AgentMessageView.hpp"
#include "AgentPage.hpp"
#include "AgentSecretEdit.hpp"
#include "AgentApprovalCard.hpp"
#include "AgentSettingsPage.hpp"
#include "AgentToolCard.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

namespace {
AgentView::Message messageFor(bool user, const QString &text, AgentView::MessageStatus status) {
    AgentView::Message message;
    message.id = QStringLiteral("m-%1").arg(text.left(4));
    message.user = user;
    message.content = text;
    message.status = status;
    return message;
}
}  // namespace

int UiAgentCheck() {
    QTemporaryDir fixture;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fixture.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, fixture.filePath("system"));
    QCoreApplication::instance()->setProperty("wtfml.testAgentDataDir", fixture.filePath("agent"));

    QMainWindow window;
    MainWindowUI ui(&window);
    window.resize(900, 620);
    window.show();
    QApplication::processEvents();

    QTabWidget *tabs = ui.getTabWidget();
    check(tabs != nullptr && tabs->count() == 5, "智能体 UI：五个选项卡",
          QString::number(tabs != nullptr ? tabs->count() : -1));
    check(tabs != nullptr && tabs->tabText(4) == QStringLiteral("智能体") &&
              MainWindowUI::agentTabIndex() == 4,
          "智能体 UI：智能体是第五颗（最后）");
    check(tabs != nullptr && tabs->currentIndex() == MainWindowUI::homeTabIndex(),
          "智能体 UI：启动仍停在主页");

    tabs->setCurrentIndex(MainWindowUI::agentTabIndex());
    QApplication::processEvents();
    AgentPage *page = ui.getAgentPage();
    check(page != nullptr && page->objectName() == MainWindowUI::agentTabName(),
          "智能体 UI：按 objectName 找得到页面");
    QWidget *sidebar = page != nullptr ? page->findChild<QWidget *>(QStringLiteral("agentSidebar"))
                                       : nullptr;
    check(sidebar != nullptr && sidebar->width() == AgentPage::sidebarWidth(),
          "智能体 UI：900 宽时侧栏 248", QString::number(sidebar != nullptr ? sidebar->width() : -1));
    // 主窗口的最小宽度由四页共同顶住（890 左右），所以"窄页面"这一档用一个不在布局里的
    // AgentPage 单独量：判据是宽度 < 760 时侧栏降到 216（见 AgentPage::resizeEvent）。
    {
        AgentPage narrowPage;
        narrowPage.show();
        QApplication::processEvents();
        narrowPage.resize(700, 500);
        QApplication::processEvents();
        QWidget *narrowSidebar =
            narrowPage.findChild<QWidget *>(QStringLiteral("agentSidebar"));
        check(narrowSidebar != nullptr &&
                  narrowSidebar->width() == AgentPage::sidebarNarrowWidth(),
              "智能体 UI：窄页面侧栏降到 216",
              QStringLiteral("侧栏 %1 / 页面宽 %2")
                  .arg(narrowSidebar != nullptr ? narrowSidebar->width() : -1)
                  .arg(narrowPage.width()));
    }

    const auto find = [page](const char *name) {
        return page != nullptr ? page->findChild<QWidget *>(QString::fromLatin1(name)) : nullptr;
    };
    check(find("agentSessionList") != nullptr && find("agentNewSessionButton") != nullptr &&
              find("agentSessionSearch") != nullptr && find("agentChatHeader") != nullptr &&
              find("agentModelCombo") != nullptr && find("agentInstanceLockChip") != nullptr &&
              find("agentApiSettingsButton") != nullptr &&
              find("agentMessageScroll") != nullptr && find("agentComposer") != nullptr &&
              find("agentSendButton") != nullptr && find("agentStopButton") != nullptr &&
              find("agentModeCombo") != nullptr && find("agentEmptyState") != nullptr &&
              find("agentEncryptSessionCheck") != nullptr,
          "智能体 UI：关键 objectName 全在");
    auto *empty = find("agentEmptyState");
    check(empty != nullptr && empty->isVisible(), "智能体 UI：空态可见");
    auto *send = qobject_cast<QPushButton *>(find("agentSendButton"));
    auto *stop = qobject_cast<QPushButton *>(find("agentStopButton"));
    check(send != nullptr && !send->isEnabled(), "智能体 UI：未配置 API 时发送禁用");
    check(stop != nullptr && !stop->isVisible(), "智能体 UI：空闲时停止按钮不露头");

    page->setConfigured(true, QStringLiteral("API 已配置"));
    page->composer()->setPlainText(QStringLiteral("你好"));
    QApplication::processEvents();
    check(send != nullptr && send->isEnabled(), "智能体 UI：配置 + 有文字后发送可用");
    page->setStreaming(true);
    QApplication::processEvents();
    check(stop != nullptr && stop->isVisible() && !send->isVisible(),
          "智能体 UI：流式中停止替代发送");
    page->setStreaming(false);

    QList<AgentView::SessionMeta> sessions;
    for (int i = 0; i < 2; ++i) {
        AgentView::SessionMeta meta;
        meta.id = QStringLiteral("s%1").arg(i);
        meta.title = QStringLiteral("会话 %1").arg(i);
        meta.updatedAt = QDateTime::currentDateTimeUtc().addSecs(-i * 60);
        meta.pinned = i == 1;
        sessions.append(meta);
    }
    page->setSessions(sessions);
    page->setCurrentSession(QStringLiteral("s0"));
    auto *list = qobject_cast<QListWidget *>(find("agentSessionList"));
    check(list != nullptr && list->count() == 2, "智能体 UI：会话列表两项");
    check(AgentPage::relativeTime(sessions.at(1).updatedAt, QDateTime::currentDateTimeUtc())
              .contains(QStringLiteral("分钟")),
          "智能体 UI：相对时间");
    page->setMessages({messageFor(true, QStringLiteral("启动就崩溃"),
                                  AgentView::MessageStatus::Complete),
                       messageFor(false,
                                  QStringLiteral("<script>alert(1)</script>\n```\ncode\n```"),
                                  AgentView::MessageStatus::Complete)});
    QApplication::processEvents();
    const QList<AgentMessageView *> views = page->findChildren<AgentMessageView *>();
    check(views.size() == 2, "智能体 UI：两条消息各有视图", QString::number(views.size()));
    bool scriptEscaped = true;
    bool codeRendered = false;
    for (AgentMessageView *view : views) {
        QLabel *body = view->findChild<QLabel *>(QStringLiteral("agentMessageBody"));
        if (body == nullptr) {
            scriptEscaped = false;
            continue;
        }
        scriptEscaped = scriptEscaped && !body->text().contains(QStringLiteral("<script"));
        codeRendered = codeRendered || body->text().contains(QStringLiteral("<pre><code>"));
    }
    check(scriptEscaped, "智能体 UI：消息里的原始 script 被转义");
    check(codeRendered, "智能体 UI：围栏代码块渲染成 pre/code");
    check(!AgentMarkdown::toHtml(QStringLiteral("<img src=x onerror=1>"))
               .contains(QStringLiteral("<img")),
          "智能体 UI：不生成远程 img");

    AgentView::ToolCard toolCard;
    toolCard.id = QStringLiteral("call-1");
    toolCard.title = QStringLiteral("查看目录概况");
    toolCard.summary = QStringLiteral("目录：2 个文件");
    toolCard.detail = QStringLiteral("{}");
    toolCard.status = AgentView::ToolCardStatus::Done;
    page->upsertToolCard(toolCard);
    QApplication::processEvents();
    auto *toolView = page->findChild<AgentToolCard *>();
    check(toolView != nullptr && toolView->cardId() == QStringLiteral("call-1"),
          "智能体 UI：工具卡可按 id 更新",
          QStringLiteral("count=%1 id=%2").arg(page->findChildren<AgentToolCard *>().size())
              .arg(toolView != nullptr ? toolView->cardId() : QStringLiteral("<null>")));
    check(toolView != nullptr &&
              toolView->findChild<QLabel *>(QStringLiteral("agentToolTitle")) != nullptr,
          "智能体 UI：工具卡有标题");

    QJsonObject approval{{QStringLiteral("approvalId"), QStringLiteral("a1")},
                         {QStringLiteral("tool"), QStringLiteral("fs.write")},
                         {QStringLiteral("displayName"), QStringLiteral("写入文本配置")},
                         {QStringLiteral("target"), QStringLiteral("config/a.toml")},
                         {QStringLiteral("summary"), QStringLiteral("写入 1 个文件")},
                         {QStringLiteral("preview"), QStringLiteral("配置预览")},
                         {QStringLiteral("diff"), QStringLiteral("- old") + QChar(10) + QStringLiteral("+ new")},
                         {QStringLiteral("backupHint"), QStringLiteral("写前备份")},
                         {QStringLiteral("sessionAllowAvailable"), false},
                         {QStringLiteral("egress"),
                          QJsonObject{{QStringLiteral("kind"), QStringLiteral("config-file")},
                                      {QStringLiteral("reason"), QStringLiteral("逐次确认")}}}};
    page->showApproval(approval);
    QApplication::processEvents();
    auto *approvalCard = page->findChild<AgentApprovalCard *>();
    check(approvalCard != nullptr && approvalCard->approvalId() == QStringLiteral("a1"),
          "智能体 UI：审批卡显示当前请求");
    check(approvalCard != nullptr &&
              approvalCard->findChild<QPushButton *>(QStringLiteral("agentApprovalAllow")) != nullptr &&
              approvalCard->findChild<QPushButton *>(QStringLiteral("agentApprovalDeny")) != nullptr,
          "智能体 UI：审批卡有允许/拒绝");
    auto *sessionAllow =
        approvalCard != nullptr
            ? approvalCard->findChild<QPushButton *>(QStringLiteral("agentApprovalSessionAllow"))
            : nullptr;
    check(sessionAllow != nullptr && !sessionAllow->isVisible(),
          "智能体 UI：Stage 1B 不提供本会话目录放行");
    auto *approvalPreview = approvalCard != nullptr
                                ? approvalCard->findChild<QPlainTextEdit *>(
                                      QStringLiteral("agentEgressPreviewCard"))
                                : nullptr;
    check(approvalPreview != nullptr &&
              approvalPreview->toPlainText().contains(QStringLiteral("new")),
          "智能体 UI：审批卡显示 diff/预览");
    page->hideApproval();
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(page->findChild<AgentApprovalCard *>() == nullptr, "智能体 UI：审批卡可收起");

    page->setEncryptionChecked(true);
    auto *encrypt = qobject_cast<QCheckBox *>(find("agentEncryptSessionCheck"));
    check(encrypt != nullptr && encrypt->isChecked(), "智能体 UI：加密开关可显示当前状态");

    AgentSettingsPage settings;
    auto *keyEdit = settings.findChild<QLineEdit *>(QStringLiteral("agentApiKeyEdit"));
    auto *reveal = settings.findChild<QToolButton *>(QStringLiteral("agentApiKeyRevealButton"));
    check(keyEdit != nullptr && keyEdit->echoMode() == QLineEdit::Password,
          "设置页：API Key 默认遮挡");
    check(reveal != nullptr, "设置页：眼睛按钮存在");
    if (reveal != nullptr) {
        reveal->setChecked(true);
        check(keyEdit->echoMode() == QLineEdit::Normal, "设置页：眼睛点开显示明文");
        reveal->setChecked(false);
        check(keyEdit->echoMode() == QLineEdit::Password, "设置页：再点一下恢复遮挡");
    }
    AgentView::ProviderDraft profile;
    profile.id = QStringLiteral("default");
    profile.name = QStringLiteral("测试服务");
    profile.baseUrl = QStringLiteral("https://api.example.com/v1");
    profile.model = QStringLiteral("test-model");
    settings.setProfile(profile, true, QStringLiteral("credential-manager"));
    check(keyEdit->text().isEmpty() && keyEdit->placeholderText().contains(QStringLiteral("留空不修改")),
          "设置页：已保存 key 时不回显，占位文字说明留空不修改");
    auto *baseUrl = settings.findChild<QLineEdit *>(QStringLiteral("agentBaseUrlEdit"));
    auto *preview = settings.findChild<QLabel *>(QStringLiteral("agentFinalUrlPreview"));
    check(baseUrl != nullptr && preview != nullptr &&
              preview->text().contains(QStringLiteral("https://api.example.com/v1/chat/completions")),
          "设置页：显示最终请求 URL 预览", preview != nullptr ? preview->text() : QString());
    auto *advanced = settings.findChild<QGroupBox *>(QStringLiteral("agentAdvancedGroup"));
    auto *authMode = settings.findChild<QComboBox *>(QStringLiteral("agentAuthModeCombo"));
    check(advanced != nullptr && !advanced->isChecked() && authMode != nullptr,
          "设置页：协议适配默认折叠");
    auto *strict = settings.findChild<QCheckBox *>(QStringLiteral("agentEgressStrictCheck"));
    check(strict != nullptr, "设置页：严格模式勾选框存在");
    settings.setStrictEgress(true);
    check(settings.strictEgress(), "设置页：严格模式可切换");
    auto *privacy = settings.findChild<QLabel *>(QStringLiteral("agentPrivacyText"));
    check(privacy != nullptr && privacy->text().contains(QStringLiteral("不读取文件")) &&
              privacy->text().contains(QStringLiteral("不做遥测")),
          "设置页：隐私/外发说明写明 Stage 1A 边界");
    check(settings.findChild<QWidget *>(QStringLiteral("agentMcpList")) == nullptr,
          "设置页：Stage 1A 不显示 MCP 一栏");
    return 0;
}
