/**
 * @file AgentPage.cpp
 * @brief 双栏聊天页的布局与交互（见 .hpp）。
 */

#include "AgentPage.hpp"

#include "AgentComposer.hpp"
#include "AgentMessageView.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
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
QString pinnedPrefix(bool pinned) { return pinned ? QStringLiteral("★ ") : QString(); }
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
    sideLayout->setContentsMargins(12, 12, 12, 12);
    sideLayout->setSpacing(8);

    m_newSession = new QPushButton(QStringLiteral("＋ 新建对话"), m_sidebar);
    m_newSession->setObjectName(QStringLiteral("agentNewSessionButton"));
    m_newSession->setFocusPolicy(Qt::StrongFocus);
    sideLayout->addWidget(m_newSession);

    m_search = new QLineEdit(m_sidebar);
    m_search->setObjectName(QStringLiteral("agentSessionSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索会话"));
    m_search->setClearButtonEnabled(true);
    sideLayout->addWidget(m_search);

    m_sessionList = new QListWidget(m_sidebar);
    m_sessionList->setObjectName(QStringLiteral("agentSessionList"));
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionList->setSpacing(2);
    sideLayout->addWidget(m_sessionList, 1);

    m_apiStatus = new QLabel(m_sidebar);
    m_apiStatus->setObjectName(QStringLiteral("agentApiStatus"));
    m_apiStatus->setWordWrap(true);
    m_apiStatus->setForegroundRole(QPalette::PlaceholderText);
    sideLayout->addWidget(m_apiStatus);
    root->addWidget(m_sidebar);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    root->addWidget(separator);

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
    m_instanceChip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    headerLayout->addWidget(m_instanceChip);

    m_modelCombo = new QComboBox(header);
    m_modelCombo->setObjectName(QStringLiteral("agentModelCombo"));
    m_modelCombo->setMinimumWidth(0);
    m_modelCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    headerLayout->addWidget(m_modelCombo);

    m_encryptCheck = new QCheckBox(QStringLiteral("加密本会话"), header);
    m_encryptCheck->setObjectName(QStringLiteral("agentEncryptSessionCheck"));
    m_encryptCheck->setToolTip(QStringLiteral("使用 DPAPI 加密本会话正文；元数据仍为明文。"));
    m_encryptCheck->setMinimumWidth(0);
    m_encryptCheck->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    headerLayout->addWidget(m_encryptCheck);

    m_settingsButton = new QPushButton(QStringLiteral("API 设置"), header);
    m_settingsButton->setObjectName(QStringLiteral("agentApiSettingsButton"));
    m_settingsButton->setMinimumWidth(0);
    headerLayout->addWidget(m_settingsButton);
    chatLayout->addWidget(header);

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
    m_emptyState = new QLabel(QStringLiteral(
        "自由对话（Stage 1A）。\n\n"
        "本模式只发送你输入的文字和系统提示；不读取文件、日志、实例元数据，"
        "也不调用任何工具。消息会发送给当前配置的模型服务，请勿粘贴密钥或令牌。"),
        m_messageHost);
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
    auto *composerRow = new QWidget(chat);
    composerRow->setObjectName(QStringLiteral("agentComposerRow"));
    auto *composerLayout = new QHBoxLayout(composerRow);
    composerLayout->setContentsMargins(0, 0, 0, 0);
    composerLayout->setSpacing(8);

    auto *composerColumn = new QWidget(composerRow);
    auto *composerColumnLayout = new QVBoxLayout(composerColumn);
    composerColumnLayout->setContentsMargins(0, 0, 0, 0);
    composerColumnLayout->setSpacing(4);

    m_modeCombo = new QComboBox(composerColumn);
    m_modeCombo->setObjectName(QStringLiteral("agentModeCombo"));
    m_modeCombo->addItem(QStringLiteral("自由对话"), QStringLiteral("general"));
    m_modeCombo->setToolTip(QStringLiteral("Stage 1A 只有自由对话；维护工作流在 Stage 2 接入。"));
    composerColumnLayout->addWidget(m_modeCombo);

    m_composer = new AgentComposer(composerColumn);
    m_composer->setMinimumHeight(34);
    composerColumnLayout->addWidget(m_composer);
    composerLayout->addWidget(composerColumn, 1);

    auto *buttonColumn = new QWidget(composerRow);
    auto *buttonLayout = new QVBoxLayout(buttonColumn);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(4);
    m_send = new QPushButton(QStringLiteral("发送"), buttonColumn);
    m_send->setObjectName(QStringLiteral("agentSendButton"));
    m_send->setDefault(true);
    m_send->setEnabled(false);
    m_stop = new QPushButton(QStringLiteral("停止"), buttonColumn);
    m_stop->setObjectName(QStringLiteral("agentStopButton"));
    m_stop->setEnabled(false);
    m_stop->hide();
    buttonLayout->addWidget(m_send);
    buttonLayout->addWidget(m_stop);
    composerLayout->addWidget(buttonColumn);
    chatLayout->addWidget(composerRow);
    root->addWidget(chat, 1);

    // ---------- 接线 ----------
    connect(m_newSession, &QPushButton::clicked, this, &AgentPage::newSessionRequested);
    connect(m_settingsButton, &QPushButton::clicked, this, &AgentPage::settingsRequested);
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
        if (item != nullptr) emit sessionSelected(item->data(Qt::UserRole).toString());
    });
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this,
            &AgentPage::showSessionMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < m_sessionList->count(); ++i) {
            QListWidgetItem *item = m_sessionList->item(i);
            item->setHidden(!text.trimmed().isEmpty() &&
                            !item->text().contains(text.trimmed(), Qt::CaseInsensitive));
        }
    });
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, &AgentPage::modelChanged);
    connect(m_encryptCheck, &QCheckBox::toggled, this, &AgentPage::encryptToggled);
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
    if (!relative.isEmpty()) text += QStringLiteral("  ·  ") + relative;
    item->setText(text);
    item->setData(Qt::UserRole, meta.id);
    item->setData(Qt::UserRole + 1, meta.pinned);
    item->setToolTip(meta.title);
    item->setData(Qt::AccessibleTextRole, QStringLiteral("会话：%1").arg(meta.title));
}

void AgentPage::setSessions(const QList<AgentView::SessionMeta> &sessions) {
    m_metaById.clear();
    m_sessionList->clear();
    for (const AgentView::SessionMeta &meta : sessions) {
        m_metaById.insert(meta.id, meta);
        auto *item = new QListWidgetItem;
        rebuildSessionRow(item, meta);
        m_sessionList->addItem(item);
        item->setData(Qt::AccessibleTextRole, QStringLiteral("会话：%1").arg(meta.title));
    }
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
    } else {
        m_title->setText(QStringLiteral("新对话"));
        QSignalBlocker blocker(m_encryptCheck);
        m_encryptCheck->setChecked(false);
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
    m_emptyState->setVisible(true);
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

void AgentPage::setEncryptionVisible(bool visible) { m_encryptCheck->setVisible(visible); }

void AgentPage::setEncryptionChecked(bool checked) {
    QSignalBlocker blocker(m_encryptCheck);
    m_encryptCheck->setChecked(checked);
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
        bool accepted = false;
        const QString title = QInputDialog::getText(this, QStringLiteral("重命名会话"),
                                                    QStringLiteral("标题"),
                                                    QLineEdit::Normal, meta.title, &accepted);
        if (accepted) emit sessionRenameRequested(id, title);
    } else if (chosen == pin) {
        emit sessionPinRequested(id, !meta.pinned);
    } else if (chosen == remove) {
        emit sessionDeleteRequested(id);
    }
}
