/**
 * @file AgentPage.cpp
 * @brief 双栏聊天页的布局与交互（见 .hpp）。
 */

#include "AgentPage.hpp"

#include "AgentApprovalCard.hpp"
#include "AgentComposer.hpp"
#include "AgentMessageView.hpp"
#include "AgentPanelPaint.hpp"
#include "AgentToolCard.hpp"
#include "InlineMessage.hpp"
#include "OverlayHost.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// 输入区那一整块：输入框 + 底下一行（提示 / 发送）合在一个圆角软面板里（设计稿 agent）。
class ComposerBox : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override { paintAgentSoftPanel(this, 14); }
};

/// 侧栏会话列表：透明底（透出整窗磨砂），选中 / 悬停只罩一层淡墨色。
void styleSessionList(QListWidget *list) {
    const auto apply = [list] {
        QPalette pal = QApplication::palette();
        const QColor ink = pal.color(QPalette::WindowText);
        pal.setColor(QPalette::Base, Qt::transparent);
        pal.setColor(QPalette::Window, Qt::transparent);
        QColor pick = ink;
        pick.setAlpha(Theme::isDark() ? 30 : 22);
        pal.setColor(QPalette::Highlight, pick);
        pal.setColor(QPalette::HighlightedText, ink);
        list->setPalette(pal);
        list->viewport()->setPalette(pal);
        list->setAutoFillBackground(false);
        list->viewport()->setAutoFillBackground(false);
        list->viewport()->update();
    };
    apply();
    Theme::follow(list, apply);
}
QString pinnedPrefix(bool pinned) { return pinned ? QStringLiteral("★ ") : QString(); }

/// 会话列表里「固定 / 今天 / 更早」那几行小标题：不可选、不可点，身份记在这个角色上。
constexpr int kGroupHeaderRole = Qt::UserRole + 2;

void tintGroupHeaders(QListWidget *list) {
    const QColor muted = QApplication::palette().color(QPalette::PlaceholderText);
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->data(kGroupHeaderRole).toBool()) item->setForeground(muted);
    }
}

/// 输入区右下那颗方形图标钮（发送 / 停止）。
void markSquareIconButton(QPushButton *button, const QString &icon, const QString &tip,
                          Theme::ButtonKind kind) {
    Theme::markButton(button, kind);
    Theme::markIconButton(button);
    button->setFixedSize(32, 32);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setCursor(Qt::PointingHandCursor);
    const auto tint = [button, icon, kind] {
        const QColor ink = kind == Theme::ButtonKind::Primary
                               ? QApplication::palette().color(QPalette::HighlightedText)
                               : Theme::tokens().text;
        button->setIcon(VersionIcons::line(icon, 16, ink));
    };
    tint();
    Theme::follow(button, tint);
}
}

AgentPage::AgentPage(QWidget *parent)
    : ThemeBackground(Theme::Part::PageBackground, parent) {
    setObjectName(QStringLiteral("agentPage"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 左侧会话栏 ----------
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("agentSidebar"));
    m_sidebar->setFixedWidth(sidebarWidth());
    auto *sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(12, 12, 4, 12);
    sideLayout->setSpacing(8);

    m_newSession = new QPushButton(QStringLiteral("＋ 新建对话"), m_sidebar);
    m_newSession->setObjectName(QStringLiteral("agentNewSessionButton"));
    m_newSession->setFocusPolicy(Qt::StrongFocus);
    Theme::markButton(m_newSession, Theme::ButtonKind::Neutral);
    sideLayout->addWidget(m_newSession);

    m_search = new QLineEdit(m_sidebar);
    m_search->setObjectName(QStringLiteral("agentSessionSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索会话"));
    m_search->setClearButtonEnabled(true);
    {
        QAction *glass = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
        const auto tint = [glass] {
            glass->setIcon(VersionIcons::line(QStringLiteral("search"), 16, Theme::tokens().secondary));
        };
        tint();
        Theme::follow(m_search, tint);
    }
    sideLayout->addWidget(m_search);

    m_sessionList = new QListWidget(m_sidebar);
    m_sessionList->setObjectName(QStringLiteral("agentSessionList"));
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionList->setSpacing(2);
    styleSessionList(m_sessionList);
    Theme::follow(m_sessionList, [list = m_sessionList] { tintGroupHeaders(list); });
    sideLayout->addWidget(m_sessionList, 1);

    m_apiStatus = new QLabel(m_sidebar);
    m_apiStatus->setObjectName(QStringLiteral("agentApiStatus"));
    m_apiStatus->setWordWrap(true);
    m_apiStatus->setForegroundRole(QPalette::PlaceholderText);
    sideLayout->addWidget(m_apiStatus);
    root->addWidget(m_sidebar);

    // ---------- 右侧聊天区 ----------
    auto *chat = new QWidget(this);
    chat->setObjectName(QStringLiteral("agentChatArea"));
    // 窄窗口要能真的缩到 700：标题/芯片/模型下拉都有长文字，默认的
    // minimumSizeHint 会把整窗顶到 890 左右。这些控件允许被压缩，文字由省略/提示补。
    chat->setMinimumWidth(0);
    chat->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *chatLayout = new QVBoxLayout(chat);
    chatLayout->setContentsMargins(16, 12, 16, 12);
    chatLayout->setSpacing(8);

    auto *header = new QWidget(chat);
    header->setObjectName(QStringLiteral("agentChatHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    m_title = new QLabel(QStringLiteral("新对话"), header);
    m_title->setObjectName(QStringLiteral("agentSessionTitle"));
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    m_title->setFont(Theme::textFont(titleFont, Theme::TextRole::Heading));
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_title, 1);

    m_instanceChip = new QPushButton(QStringLiteral("跟随主页：未选择实例"), header);
    m_instanceChip->setObjectName(QStringLiteral("agentInstanceLockChip"));
    m_instanceChip->setFlat(true);
    m_instanceChip->setFocusPolicy(Qt::StrongFocus);
    m_instanceChip->setMinimumWidth(0);
    m_instanceChip->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    Theme::markChipButton(m_instanceChip);

    m_modelCombo = new QComboBox(header);
    m_modelCombo->setObjectName(QStringLiteral("agentModelCombo"));
    m_modelCombo->setMinimumWidth(0);
    m_modelCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    headerLayout->addWidget(m_modelCombo);

    m_settingsButton = new QPushButton(QStringLiteral("API 设置"), header);
    m_settingsButton->setObjectName(QStringLiteral("agentApiSettingsButton"));
    m_settingsButton->setMinimumWidth(0);
    Theme::markButton(m_settingsButton, Theme::ButtonKind::Ghost);
    {
        const auto tint = [b = m_settingsButton] {
            b->setIcon(VersionIcons::line(QStringLiteral("gear"), 16, Theme::tokens().secondary));
        };
        tint();
        Theme::follow(m_settingsButton, tint);
    }
    headerLayout->addWidget(m_settingsButton);
    chatLayout->addWidget(header);

    // 会话加密是用户最容易找不到的功能之一：单独占一行，写清“当前是明文还是已加密”，
    // 不要挤在右上角那排下拉框里。
    m_sessionOptions = new QWidget(chat);
    m_sessionOptions->setObjectName(QStringLiteral("agentSessionOptions"));
    auto *optionsLayout = new QHBoxLayout(m_sessionOptions);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(8);
    // UI 7d：标题下一行 chip —— 跟随实例 / 模式 / 加密（设计稿 agent）。
    m_instanceChip->setParent(m_sessionOptions);
    optionsLayout->addWidget(m_instanceChip);
    // 精修：勾选框只留两个字，「DPAPI 加密什么」进悬停提示；右边那句状态也只留结论。
    m_encryptCheck = new QCheckBox(QStringLiteral("加密"), m_sessionOptions);
    m_encryptCheck->setObjectName(QStringLiteral("agentEncryptSessionCheck"));
    m_encryptCheck->setToolTip(
        QStringLiteral("用当前 Windows 用户的 DPAPI 加密本会话正文；元数据仍为明文。"));
    m_encryptCheck->setAccessibleName(QStringLiteral("加密本会话"));
    // 开关只画胶囊不画字：前面单摆一个「加密」小字当行名（和设置页的开关行同一种写法）。
    Theme::markSwitch(m_encryptCheck);
    auto *encryptLabel = new QLabel(QStringLiteral("加密"), m_sessionOptions);
    encryptLabel->setObjectName(QStringLiteral("agentEncryptSessionLabel"));
    encryptLabel->setBuddy(m_encryptCheck);
    encryptLabel->setToolTip(m_encryptCheck->toolTip());
    optionsLayout->addSpacing(6);
    optionsLayout->addWidget(encryptLabel);
    optionsLayout->addWidget(m_encryptCheck);
    m_encryptionStatus = new QLabel(m_sessionOptions);
    m_encryptionStatus->setObjectName(QStringLiteral("agentEncryptionStatus"));
    m_encryptionStatus->setForegroundRole(QPalette::PlaceholderText);
    optionsLayout->addWidget(m_encryptionStatus);
    optionsLayout->addStretch(1);
    chatLayout->addWidget(m_sessionOptions);

    // ---------- 消息区 ----------
    m_scroll = new QScrollArea(chat);
    m_scroll->setObjectName(QStringLiteral("agentMessageScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageHost = new QWidget(m_scroll);
    m_messageHost->setObjectName(QStringLiteral("agentMessageHost"));
    m_messageLayout = new QVBoxLayout(m_messageHost);
    m_messageLayout->setContentsMargins(0, 0, 0, 0);
    m_messageLayout->setSpacing(8);
    m_emptyState = new QLabel(QStringLiteral("自由对话：只发送你输入的文字，不读文件、不调用工具。"),
                              m_messageHost);
    m_emptyState->setToolTip(QStringLiteral(
        "自由对话（Stage 1A）。本模式只发送你输入的文字和系统提示；不读取文件、日志、实例元数据，"
        "也不调用任何工具。消息会发送给当前配置的模型服务，请勿粘贴密钥或令牌。"));
    m_emptyState->setObjectName(QStringLiteral("agentEmptyState"));
    m_emptyState->setWordWrap(true);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setForegroundRole(QPalette::PlaceholderText);
    m_messageLayout->addWidget(m_emptyState);
    m_messageLayout->addStretch(1);
    m_scroll->setWidget(m_messageHost);
    chatLayout->addWidget(m_scroll, 1);

    m_notice = new QLabel(chat);
    m_notice->setObjectName(QStringLiteral("agentNotice"));
    m_notice->setWordWrap(true);
    m_notice->hide();
    chatLayout->addWidget(m_notice);

    // ---------- 输入区 ----------
    auto *composerRow = new ComposerBox(chat);
    composerRow->setObjectName(QStringLiteral("agentComposerRow"));
    auto *composerLayout = new QVBoxLayout(composerRow);
    composerLayout->setContentsMargins(12, 8, 10, 8);
    composerLayout->setSpacing(4);

    // 模式是一排 chip（设计稿 agent），不是下拉框。今天只有「自由对话」一档，选中态常亮；
    // 维护工作流接进来时往这一排再添 chip，同组互斥。
    m_modeChip = new QPushButton(QStringLiteral("自由对话"), m_sessionOptions);
    m_modeChip->setObjectName(QStringLiteral("agentModeChip"));
    m_modeChip->setProperty("wtfmlAgentMode", QStringLiteral("general"));
    m_modeChip->setCheckable(true);
    m_modeChip->setChecked(true);
    m_modeChip->setToolTip(QStringLiteral(
        "自由对话：只发送你输入的文字，不读文件、不调用工具。维护工作流在 Stage 2 接入。"));
    Theme::markChipButton(m_modeChip);
    // 唯一一档：点了也不许把自己点灭。
    connect(m_modeChip, &QPushButton::toggled, m_modeChip, [chip = m_modeChip](bool on) {
        if (!on) chip->setChecked(true);
    });
    qobject_cast<QHBoxLayout *>(m_sessionOptions->layout())->insertWidget(1, m_modeChip);

    m_composer = new AgentComposer(composerRow);
    m_composer->setMinimumHeight(34);
    // 输入框默认 sizeHint 有 190 来高，整块就占了半屏：压到约三行，长文自己滚。
    m_composer->setMaximumHeight(m_composer->fontMetrics().lineSpacing() * 3 + 14);
    m_composer->setFrameShape(QFrame::NoFrame);
    {
        QPalette pal = m_composer->palette();
        pal.setColor(QPalette::Base, Qt::transparent);
        m_composer->setPalette(pal);
        m_composer->viewport()->setAutoFillBackground(false);
    }
    composerLayout->addWidget(m_composer, 1);

    auto *composerBar = new QHBoxLayout;
    composerBar->setContentsMargins(0, 0, 0, 0);
    composerBar->setSpacing(8);
    composerBar->addStretch(1);
    auto *keyHint = new QLabel(QStringLiteral("Enter 发送 · Shift+Enter 换行"), composerRow);
    keyHint->setObjectName(QStringLiteral("agentComposerHint"));
    keyHint->setFont(Theme::textFont(keyHint->font(), Theme::TextRole::Caption));
    keyHint->setForegroundRole(QPalette::PlaceholderText);
    composerBar->addWidget(keyHint);
    // 精修：发送 / 停止是右下角一颗方形图标钮（设计稿 agent），字进悬停提示。
    m_send = new QPushButton(composerRow);
    m_send->setObjectName(QStringLiteral("agentSendButton"));
    m_send->setDefault(true);
    m_send->setEnabled(false);
    markSquareIconButton(m_send, QStringLiteral("send"), QStringLiteral("发送"),
                         Theme::ButtonKind::Primary);
    m_stop = new QPushButton(composerRow);
    m_stop->setObjectName(QStringLiteral("agentStopButton"));
    markSquareIconButton(m_stop, QStringLiteral("stop"), QStringLiteral("停止"),
                         Theme::ButtonKind::Neutral);
    m_stop->setEnabled(false);
    m_stop->hide();
    composerBar->addWidget(m_send);
    composerBar->addWidget(m_stop);
    composerLayout->addLayout(composerBar);
    chatLayout->addWidget(composerRow);
    root->addWidget(chat, 1);

    // ---------- 接线 ----------
    connect(m_newSession, &QPushButton::clicked, this, &AgentPage::newSessionRequested);
    connect(m_settingsButton, &QPushButton::clicked, this, &AgentPage::settingsRequested);
    connect(m_instanceChip, &QPushButton::clicked, this, &AgentPage::instanceChipRequested);
    connect(m_send, &QPushButton::clicked, this, [this] {
        if (m_configured) emit sendRequested(m_composer->toPlainText());
    });
    connect(m_stop, &QPushButton::clicked, this, &AgentPage::stopRequested);
    connect(m_composer, &AgentComposer::sendRequested, this, [this] {
        if (m_configured && !m_streaming) emit sendRequested(m_composer->toPlainText());
    });
    connect(m_composer, &QPlainTextEdit::textChanged, this, [this] {
        updateSendEnabled();
        emit textChanged();
    });
    connect(m_sessionList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item != nullptr && !item->data(kGroupHeaderRole).toBool())
            emit sessionSelected(item->data(Qt::UserRole).toString());
    });
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this,
            &AgentPage::showSessionMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString needle = text.trimmed();
        // 倒着走：先定每条会话藏不藏，走到小标题时就知道它那一组还剩没剩。
        bool groupHasVisible = false;
        for (int i = m_sessionList->count() - 1; i >= 0; --i) {
            QListWidgetItem *item = m_sessionList->item(i);
            if (item->data(kGroupHeaderRole).toBool()) {
                item->setHidden(!groupHasVisible);
                groupHasVisible = false;
                continue;
            }
            const bool hidden = !needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive);
            item->setHidden(hidden);
            groupHasVisible = groupHasVisible || !hidden;
        }
    });
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &AgentPage::modelChanged);
    connect(m_encryptCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_encryptionStatus->setText(checked ? QStringLiteral("已加密（DPAPI）")
                                           : QStringLiteral("明文 JSON"));
        emit encryptToggled(checked);
    });
    setConfigured(false, QStringLiteral("API 未配置"));
}

int AgentPage::sidebarWidth() { return 248; }
int AgentPage::sidebarNarrowWidth() { return 216; }

QString AgentPage::relativeTime(const QDateTime &when, const QDateTime &now) {
    if (!when.isValid()) return QString();
    const qint64 seconds = when.secsTo(now);
    if (seconds < 60) return QStringLiteral("刚刚");
    if (seconds < 3600) return QStringLiteral("%1 分钟前").arg(seconds / 60);
    if (seconds < 86400) return QStringLiteral("%1 小时前").arg(seconds / 3600);
    if (seconds < 7 * 86400) return QStringLiteral("%1 天前").arg(seconds / 86400);
    return when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
}

void AgentPage::rebuildSessionRow(QListWidgetItem *item, const AgentView::SessionMeta &meta) {
    if (item == nullptr) return;
    QString text = pinnedPrefix(meta.pinned) + (meta.title.isEmpty() ? QStringLiteral("新对话") : meta.title);
    const QString relative = relativeTime(meta.updatedAt, QDateTime::currentDateTimeUtc());
    if (!relative.isEmpty()) text += QLatin1Char(10) + relative;  // 两行：标题 / 时间（设计稿 agent）
    item->setText(text);
    item->setData(Qt::UserRole, meta.id);
    item->setData(Qt::UserRole + 1, meta.pinned);
    item->setToolTip(meta.title);
    item->setData(Qt::AccessibleTextRole, QStringLiteral("会话：%1").arg(meta.title));
}

void AgentPage::setSessions(const QList<AgentView::SessionMeta> &sessions) {
    m_metaById.clear();
    m_sessionList->clear();
    // 分三组（设计稿 agent）：固定 / 今天 / 更早。组内保持传进来的先后；空组不出小标题。
    const QDate today = QDate::currentDate();
    QList<AgentView::SessionMeta> groups[3];
    for (const AgentView::SessionMeta &meta : sessions) {
        const int g = meta.pinned ? 0 : (meta.updatedAt.toLocalTime().date() == today ? 1 : 2);
        groups[g].append(meta);
    }
    const QString titles[3] = {QStringLiteral("固定"), QStringLiteral("今天"), QStringLiteral("更早")};
    const QFont headerFont = Theme::textFont(m_sessionList->font(), Theme::TextRole::Caption);
    for (int g = 0; g < 3; ++g) {
        if (groups[g].isEmpty()) continue;
        auto *header = new QListWidgetItem(titles[g]);
        header->setFlags(Qt::NoItemFlags);
        header->setData(kGroupHeaderRole, true);
        header->setFont(headerFont);
        m_sessionList->addItem(header);
        for (const AgentView::SessionMeta &meta : std::as_const(groups[g])) {
            m_metaById.insert(meta.id, meta);
            auto *item = new QListWidgetItem;
            rebuildSessionRow(item, meta);
            m_sessionList->addItem(item);
        }
    }
    tintGroupHeaders(m_sessionList);
    if (!m_currentSessionId.isEmpty()) {
        for (int i = 0; i < m_sessionList->count(); ++i) {
            if (m_sessionList->item(i)->data(Qt::UserRole).toString() == m_currentSessionId) {
                m_sessionList->setCurrentRow(i);
                break;
            }
        }
        const AgentView::SessionMeta meta = m_metaById.value(m_currentSessionId);
        if (meta.isValid()) {
            m_encryptCheck->setChecked(meta.encrypted);
            m_encryptionStatus->setText(meta.encrypted
                                            ? QStringLiteral("已加密（DPAPI）")
                                            : QStringLiteral("明文 JSON"));
            m_title->setText(meta.title);
        }
    }
}

void AgentPage::setCurrentSession(const QString &sessionId) {
    m_currentSessionId = sessionId;
    clearMessages();
    for (int i = 0; i < m_sessionList->count(); ++i) {
        if (m_sessionList->item(i)->data(Qt::UserRole).toString() == sessionId) {
            m_sessionList->setCurrentRow(i);
            break;
        }
    }
    const AgentView::SessionMeta meta = m_metaById.value(sessionId);
    if (meta.isValid()) {
        m_title->setText(meta.title);
        QSignalBlocker blocker(m_encryptCheck);
        m_encryptCheck->setChecked(meta.encrypted);
        m_encryptionStatus->setText(meta.encrypted
                                        ? QStringLiteral("已加密（DPAPI）")
                                        : QStringLiteral("明文 JSON"));
    } else {
        m_title->setText(QStringLiteral("新对话"));
        QSignalBlocker blocker(m_encryptCheck);
        m_encryptCheck->setChecked(false);
        m_encryptionStatus->setText(QStringLiteral("明文 JSON"));
    }
    m_emptyState->setVisible(sessionId.isEmpty() || m_views.isEmpty());
}

void AgentPage::setTitle(const QString &title) {
    m_title->setText(title);
}

void AgentPage::setMessages(const QList<AgentView::Message> &messages) {
    clearMessages();
    for (const AgentView::Message &message : messages) {
        upsertMessage(message);
    }
    m_emptyState->setVisible(m_views.isEmpty());
    scrollToLatest();
}

void AgentPage::upsertMessage(const AgentView::Message &message) {
    AgentMessageView *view = viewFor(message.id);
    if (view == nullptr) {
        view = new AgentMessageView(message, m_messageHost);
        connect(view, &AgentMessageView::retryRequested, this, &AgentPage::retryRequested);
        m_views.insert(message.id, view);
        m_messageLayout->insertWidget(qMax(0, m_messageLayout->count() - 1), view);
    } else {
        view->setMessage(message);
    }
    m_emptyState->setVisible(false);
    scrollToLatest();
}

void AgentPage::removeMessage(const QString &messageId) {
    AgentMessageView *view = m_views.take(messageId);
    if (view == nullptr) return;
    m_messageLayout->removeWidget(view);
    view->deleteLater();
    m_emptyState->setVisible(m_views.isEmpty());
}

void AgentPage::clearMessages() {
    const auto views = m_views;
    m_views.clear();
    for (AgentMessageView *view : views) {
        m_messageLayout->removeWidget(view);
        view->deleteLater();
    }
    clearToolCards();
    hideApproval();
    m_emptyState->setVisible(true);
}

void AgentPage::setToolCards(const QList<AgentView::ToolCard> &cards) {
    clearToolCards();
    for (const AgentView::ToolCard &card : cards) upsertToolCard(card);
}

void AgentPage::upsertToolCard(const AgentView::ToolCard &card) {
    AgentToolCard *view = m_toolCards.value(card.id, nullptr);
    if (view == nullptr) {
        view = new AgentToolCard(card, m_messageHost);
        m_toolCards.insert(card.id, view);
        m_messageLayout->insertWidget(qMax(0, m_messageLayout->count() - 1), view);
    } else {
        view->setCard(card);
    }
    m_emptyState->setVisible(false);
    scrollToLatest();
}

void AgentPage::clearToolCards() {
    const auto cards = m_toolCards;
    m_toolCards.clear();
    for (AgentToolCard *card : cards) {
        m_messageLayout->removeWidget(card);
        card->deleteLater();
    }
}

void AgentPage::showApproval(const QJsonObject &request) {
    if (m_approvalCard == nullptr) {
        m_approvalCard = new AgentApprovalCard(m_messageHost);
        connect(m_approvalCard, &AgentApprovalCard::allowed, this,
                &AgentPage::approvalAllowed);
        connect(m_approvalCard, &AgentApprovalCard::denied, this,
                &AgentPage::approvalDenied);
        m_messageLayout->insertWidget(qMax(0, m_messageLayout->count() - 1), m_approvalCard);
    }
    m_approvalCard->setRequest(request);
    m_approvalCard->show();
    scrollToLatest();
}

void AgentPage::hideApproval() {
    if (m_approvalCard == nullptr) return;
    m_messageLayout->removeWidget(m_approvalCard);
    m_approvalCard->deleteLater();
    m_approvalCard = nullptr;
}

void AgentPage::setStreaming(bool streaming) {
    m_streaming = streaming;
    m_stop->setVisible(streaming);
    m_stop->setEnabled(streaming);
    m_send->setVisible(!streaming);
    updateSendEnabled();
}

void AgentPage::setConfigured(bool configured, const QString &statusText) {
    m_configured = configured;
    if (!statusText.isEmpty()) {
        m_apiStatus->setText(statusText);
    }
    updateSendEnabled();
}

void AgentPage::setModelOptions(const QStringList &labels, const QString &current) {
    QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();
    m_modelCombo->addItems(labels);
    if (!current.isEmpty()) {
        const int index = m_modelCombo->findText(current);
        if (index >= 0) m_modelCombo->setCurrentIndex(index);
    }
}

void AgentPage::setInstanceChip(const QString &text, bool locked) {
    m_instanceChip->setText(text);
    m_instanceChip->setProperty("locked", locked);
}

void AgentPage::setEncryptionVisible(bool visible) {
    if (m_sessionOptions != nullptr) m_sessionOptions->setVisible(visible);
}

void AgentPage::setEncryptionChecked(bool checked) {
    QSignalBlocker blocker(m_encryptCheck);
    m_encryptCheck->setChecked(checked);
    m_encryptionStatus->setText(checked ? QStringLiteral("已加密（DPAPI）")
                                       : QStringLiteral("明文 JSON"));
}

void AgentPage::showNotice(const QString &text) {
    m_notice->setText(text);
    m_notice->setVisible(!text.isEmpty());
}

void AgentPage::showEmptyState(bool empty) { m_emptyState->setVisible(empty); }

void AgentPage::updateSendEnabled() {
    const bool hasText = !m_composer->toPlainText().trimmed().isEmpty();
    m_send->setEnabled(m_configured && hasText && !m_streaming);
}

void AgentPage::scrollToLatest() {
    QTimer::singleShot(0, this, [this] {
        if (m_scroll != nullptr) {
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
        }
    });
}

AgentMessageView *AgentPage::viewFor(const QString &messageId) {
    return m_views.value(messageId, nullptr);
}

void AgentPage::resizeEvent(QResizeEvent *event) {
    ThemeBackground::resizeEvent(event);
    const int width = event->size().width() < 760 ? sidebarNarrowWidth() : sidebarWidth();
    if (m_sidebar->width() != width) {
        m_sidebar->setFixedWidth(width);
    }
}

void AgentPage::showSessionMenu(const QPoint &position) {
    QListWidgetItem *item = m_sessionList->itemAt(position);
    if (item == nullptr) return;
    const QString id = item->data(Qt::UserRole).toString();
    const AgentView::SessionMeta meta = m_metaById.value(id);
    if (!meta.isValid()) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("agentSessionMenu"));
    QAction *rename = menu.addAction(QStringLiteral("重命名"));
    QAction *pin = menu.addAction(meta.pinned ? QStringLiteral("取消固定") : QStringLiteral("固定"));
    menu.addSeparator();
    QAction *remove = menu.addAction(QStringLiteral("删除"));
    QAction *chosen = menu.exec(m_sessionList->viewport()->mapToGlobal(position));
    if (chosen == rename) {
        // 问名字用内容区里那张提示卡，不另开带系统标题栏的 QInputDialog。本页挂在主窗的页签里，
        // 覆盖层是页签旁边那一个（centralWidget 的直接子控件），所以往上逐层找直接子控件。
        OverlayHost *host = OverlayHost::forWidget(this);
        for (QWidget *w = parentWidget(); host == nullptr && w != nullptr; w = w->parentWidget())
            host = w->findChild<OverlayHost *>(QString(), Qt::FindDirectChildrenOnly);
        bool accepted = false;
        const QString title = InlineMessage::getText(host, QStringLiteral("重命名会话"),
                                                     QStringLiteral("标题"), meta.title, &accepted);
        if (accepted) emit sessionRenameRequested(id, title);
    } else if (chosen == pin) {
        emit sessionPinRequested(id, !meta.pinned);
    } else if (chosen == remove) {
        emit sessionDeleteRequested(id);
    }
}
