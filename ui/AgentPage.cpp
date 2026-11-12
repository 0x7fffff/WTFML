/**
 * @file AgentPage.cpp
 * @brief 双栏聊天页的布局与交互（见 .hpp）。
 */

#include "AgentPage.hpp"

#include "AgentApprovalCard.hpp"
#include "AgentComposer.hpp"
#include "AgentMessageView.hpp"
#include "AgentToolCard.hpp"
#include "GlassPopover.hpp"
#include "InlineMessage.hpp"
#include "OverlayHost.hpp"
#include "Popups.hpp"
#include "Theme.hpp"
#include "VersionIcons.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kColumnMax = 760;              // 对话流与输入岛最宽（样张 .col / .composer）
constexpr int kGroupHeaderRole = Qt::UserRole + 2;  // 「固定 / 今天 / 更早」小标题行
constexpr int kTimeRole = Qt::UserRole + 3;         // 会话行右侧的相对时间

QColor accentInk() { return Theme::textTone(Theme::tokens().accentInk, Theme::tokens().deep); }

QFont pxFont(const QFont &base, qreal px, int weight = QFont::Normal) {
    QFont f = base;
    f.setPixelSize(qRound(px));
    f.setWeight(QFont::Weight(weight));
    return f;
}

void setInk(QWidget *w, const QColor &ink) {
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, ink);
    p.setColor(QPalette::ButtonText, ink);
    w->setPalette(p);
}

/// 会话列表的行：小标题 12px/600 淡字；会话 40 高，标题省略 + 右侧时间；选中 = 强调淡底圆角 10，不画条。
class SessionDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        return {0, index.data(kGroupHeaderRole).toBool() ? 32 : 40};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const Theme::Tokens t = Theme::tokens();
        const QRect r = option.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (index.data(kGroupHeaderRole).toBool()) {
            p->setFont(pxFont(option.font, 12, QFont::DemiBold));
            p->setPen(t.secondary);
            p->drawText(r.adjusted(10, 0, -10, -6), Qt::AlignLeft | Qt::AlignBottom, index.data().toString());
            p->restore();
            return;
        }
        const bool on = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        if (on || hover) {
            p->setPen(Qt::NoPen);
            p->setBrush(on ? t.selected : t.hover);
            p->drawRoundedRect(QRectF(r), 10, 10);
        }
        const QColor ink = on ? accentInk() : t.text;
        const QString time = index.data(kTimeRole).toString();
        const QFont small = pxFont(option.font, 12.5);
        const int timeWidth = time.isEmpty() ? 0 : QFontMetrics(small).horizontalAdvance(time);
        const QRect body = r.adjusted(10, 0, -10, 0);
        if (timeWidth > 0) {
            QColor timeInk = on ? ink : t.secondary;
            if (on) timeInk.setAlphaF(0.75);
            p->setFont(small);
            p->setPen(timeInk);
            p->drawText(body, Qt::AlignRight | Qt::AlignVCenter, time);
        }
        const QFont titleFont = pxFont(option.font, 14);
        const QRect titleBox = body.adjusted(0, 0, -(timeWidth > 0 ? timeWidth + 10 : 0), 0);
        p->setFont(titleFont);
        p->setPen(ink);
        p->drawText(titleBox, Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(titleFont).elidedText(index.data().toString(), Qt::ElideRight, titleBox.width()));
        p->restore();
    }
};

/// 输入岛：磨砂圆角 20（样张 .composer.glass），和浮层同一块面。
class ComposerBox final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        GlassPopover::paintSurface(p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 20, false);
    }
};

/// 系统提示两侧那道细线（最长 120）。
class HairLine final : public QWidget {
public:
    explicit HairLine(QWidget *parent) : QWidget(parent) {
        setFixedHeight(1);
        setMaximumWidth(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *) override { QPainter(this).fillRect(rect(), Theme::tokens().divider); }
};

/// 右上角 ⋯ 菜单与实例下拉共用的小浮层：`Qt::Popup` 顶层窗，底是 Popups 那块磨砂面，
/// 行是 34 高、圆角 8 的扁平按钮（选中 = 强调淡底 + 强调字，不打勾；危险项红字）。
class PopMenu final : public QFrame {
public:
    explicit PopMenu(QWidget *owner, const QString &name, int width) : QFrame(owner, Qt::Popup) {
        setObjectName(name);
        setFixedWidth(width);
        auto *box = new QVBoxLayout(this);
        box->setContentsMargins(6, 6, 6, 6);
        box->setSpacing(0);
        Popups::glassify(this, GlassPopover::kRadius);
    }
    QPushButton *addRow(const QString &name, const QString &icon, const QString &text, bool on = false,
                        bool danger = false) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(name);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(34);
        b->setProperty("on", on);
        Theme::markIconButton(b);
        const auto style = [b, icon, on, danger] {
            const Theme::Tokens t = Theme::tokens();
            const QColor ink = danger ? Theme::textTone(t.dangerInk, t.danger) : on ? accentInk() : t.text;
            b->setStyleSheet(QStringLiteral("QPushButton { text-align: left; padding: 0 12px; border: none; border-radius: 8px;"
                                            " background: %1; color: %2; }"
                                            "QPushButton:hover { background: %3; }")
                                 .arg(on ? t.selected.name(QColor::HexArgb) : QStringLiteral("transparent"),
                                      ink.name(QColor::HexArgb),
                                      (on ? t.selected : t.hover).name(QColor::HexArgb)));
            if (!icon.isEmpty()) {
                b->setIcon(VersionIcons::line(icon, 18, danger ? ink : t.secondary));
                b->setIconSize(QSize(18, 18));
            }
        };
        style();
        Theme::follow(b, style);
        layout()->addWidget(b);
        return b;
    }
    void addHead(const QString &text) {
        auto *l = new QLabel(text, this);
        l->setFont(pxFont(font(), 12, QFont::DemiBold));
        l->setContentsMargins(12, 10, 12, 6);
        setInk(l, Theme::tokens().secondary);
        layout()->addWidget(l);
    }
    void addDivider() {
        auto *line = new HairLine(this);
        line->setMaximumWidth(QWIDGETSIZE_MAX);
        line->setContentsMargins(0, 0, 0, 0);
        auto *wrap = new QWidget(this);
        auto *h = new QHBoxLayout(wrap);
        h->setContentsMargins(8, 6, 8, 6);
        h->addWidget(line);
        layout()->addWidget(wrap);
    }
    /// 贴在锚点下方 6px、右缘对齐锚点，出不了窗口。
    void popBelow(QWidget *anchor) {
        adjustSize();
        const QRect win = anchor->window()->geometry();
        QPoint at = anchor->mapToGlobal(QPoint(anchor->width() - width(), anchor->height() + 6));
        at.setX(qBound(win.left() + 8, at.x(), qMax(win.left() + 8, win.right() - width() - 8)));
        move(at);
        show();
        raise();
        activateWindow();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        Popups::paintSurface(p, QRectF(rect()), GlassPopover::kRadius);
    }
};

/// 图标钮（新建 / ⋯ / ⚙）：Ghost、定边长、字进悬停提示。
QPushButton *iconButton(QWidget *parent, const QString &name, const QString &icon, const QString &tip, int side,
                        int glyph) {
    auto *b = new QPushButton(parent);
    b->setObjectName(name);
    Theme::markButton(b, Theme::ButtonKind::Ghost);
    Theme::markIconButton(b);
    b->setFixedSize(side, side);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setCursor(Qt::PointingHandCursor);
    const auto tint = [b, icon, glyph] {
        b->setIcon(VersionIcons::line(icon, glyph, Theme::tokens().secondary, icon == QLatin1String("more") ? 2.4 : 1.6));
        b->setIconSize(QSize(glyph, glyph));
    };
    tint();
    Theme::follow(b, tint);
    return b;
}

/// 圆形发送 / 停止钮（输入岛右下）。
void markRoundButton(QPushButton *button, const QString &icon, const QString &tip, Theme::ButtonKind kind) {
    Theme::markButton(button, kind);
    Theme::markIconButton(button);
    button->setFixedSize(36, 36);
    button->setStyleSheet(QStringLiteral("QPushButton { border-radius: 18px; padding: 0; }"));
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setCursor(Qt::PointingHandCursor);
    const auto tint = [button, icon, kind] {
        const QColor ink = kind == Theme::ButtonKind::Primary ? Theme::tokens().onPrimary : Theme::tokens().text;
        button->setIcon(VersionIcons::line(icon, 18, ink, 2.0));
        button->setIconSize(QSize(18, 18));
    };
    tint();
    Theme::follow(button, tint);
}

/// 胶囊（实例下拉 / API 警示）：定高、圆角全、底色自己挂。
void styleCapsule(QPushButton *b, int height, std::function<QColor()> fill, std::function<QColor()> hover,
                  std::function<QColor()> ink) {
    Theme::markIconButton(b);
    b->setFixedHeight(height);
    b->setCursor(Qt::PointingHandCursor);
    const auto apply = [b, height, fill, hover, ink] {
        b->setStyleSheet(QStringLiteral("QPushButton { border: none; border-radius: %1px; padding: 0 12px; background: %2;"
                                        " color: %3; }"
                                        "QPushButton:hover, QPushButton[open=\"true\"] { background: %4; }")
                             .arg(height / 2)
                             .arg(fill().name(QColor::HexArgb), ink().name(QColor::HexArgb),
                                  hover().name(QColor::HexArgb)));
    };
    apply();
    Theme::follow(b, apply);
}

void markOpen(QPushButton *anchor, bool open) {
    anchor->setProperty("open", open);
    anchor->style()->unpolish(anchor);
    anchor->style()->polish(anchor);
}

}  // namespace

AgentPage::AgentPage(QWidget *parent)
    : ThemeBackground(Theme::Part::PageBackground, parent) {
    setObjectName(QStringLiteral("agentPage"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 左侧会话栏（直接放在玻璃上，不加底）----------
    // 页面顶上就是 40 高的拖动区下沿：第一行从 12 开始（tokens-r2 §8，体验反馈 18）。
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("agentSidebar"));
    m_sidebar->setFixedWidth(sidebarWidth());
    auto *sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(12, 12, 8, 14);
    sideLayout->setSpacing(0);

    auto *sideHead = new QHBoxLayout;
    sideHead->setContentsMargins(10, 0, 2, 0);
    auto *sideTitle = new QLabel(QStringLiteral("会话"), m_sidebar);
    sideTitle->setObjectName(QStringLiteral("agentSidebarTitle"));
    sideTitle->setFont(pxFont(sideTitle->font(), 16, QFont::DemiBold));
    sideHead->addWidget(sideTitle, 1);
    m_newSession = iconButton(m_sidebar, QStringLiteral("agentNewSessionButton"), QStringLiteral("plus"),
                              QStringLiteral("新建对话"), 32, 18);
    sideHead->addWidget(m_newSession);
    auto *sideHeadBox = new QWidget(m_sidebar);
    sideHeadBox->setFixedHeight(36);
    sideHeadBox->setLayout(sideHead);
    sideLayout->addWidget(sideHeadBox);
    sideLayout->addSpacing(8);

    m_search = new QLineEdit(m_sidebar);
    m_search->setObjectName(QStringLiteral("agentSessionSearch"));
    m_search->setPlaceholderText(QStringLiteral("搜索会话"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(34);
    {
        QAction *glass = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
        const auto tint = [glass] {
            glass->setIcon(VersionIcons::line(QStringLiteral("search"), 16, Theme::tokens().secondary));
        };
        tint();
        Theme::follow(m_search, tint);
    }
    sideLayout->addWidget(m_search);
    sideLayout->addSpacing(2);

    m_sessionList = new QListWidget(m_sidebar);
    m_sessionList->setObjectName(QStringLiteral("agentSessionList"));
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sessionList->setMouseTracking(true);
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sessionList->setItemDelegate(new SessionDelegate(m_sessionList));
    m_sessionList->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; outline: none; }"));
    m_sessionList->viewport()->setAutoFillBackground(false);
    sideLayout->addWidget(m_sessionList, 1);
    root->addWidget(m_sidebar);

    // ---------- 右侧聊天区 ----------
    auto *chat = new QWidget(this);
    chat->setObjectName(QStringLiteral("agentChatArea"));
    // 窄窗口要能真的缩下去：标题 / 胶囊都有长文字，允许被压缩，文字由省略 / 提示补。
    chat->setMinimumWidth(0);
    chat->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *chatLayout = new QVBoxLayout(chat);
    chatLayout->setContentsMargins(24, 12, 20, 20);
    chatLayout->setSpacing(0);

    // 顶上一行：标题 · 警示胶囊 · 实例 · ⋯ · ⚙（样张 .top，高 36）
    auto *header = new QWidget(chat);
    header->setObjectName(QStringLiteral("agentChatHeader"));
    header->setFixedHeight(36);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);
    m_title = new QLabel(QStringLiteral("新对话"), header);
    m_title->setObjectName(QStringLiteral("agentSessionTitle"));
    m_title->setFont(pxFont(m_title->font(), 16, QFont::DemiBold));
    m_title->setMinimumWidth(0);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setToolTip(QStringLiteral("双击重命名"));
    m_title->installEventFilter(this);
    headerLayout->addWidget(m_title, 1);

    m_apiWarn = new QPushButton(QStringLiteral("API 未配置"), header);
    m_apiWarn->setObjectName(QStringLiteral("agentApiWarnChip"));
    m_apiWarn->setFont(pxFont(m_apiWarn->font(), 13, QFont::Medium));
    styleCapsule(
        m_apiWarn, 28, [] { return Theme::tokens().warnSoft; },
        [] {
            QColor c = Theme::tokens().warnSoft;
            c.setAlphaF(qMin(1.0, c.alphaF() * 1.6));
            return c;
        },
        [] { return Theme::textTone(Theme::tokens().warnInk, Theme::tokens().warn); });
    {
        const auto tint = [b = m_apiWarn] {
            b->setIcon(VersionIcons::line(QStringLiteral("warn"), 15,
                                          Theme::textTone(Theme::tokens().warnInk, Theme::tokens().warn), 1.8));
            b->setIconSize(QSize(15, 15));
        };
        tint();
        Theme::follow(m_apiWarn, tint);
    }
    headerLayout->addWidget(m_apiWarn);

    // 「实例」下拉胶囊：左半「实例」淡字，右半当前值；整块是一颗按钮（两颗字不接鼠标）。
    m_instanceChip = new QPushButton(header);
    m_instanceChip->setObjectName(QStringLiteral("agentInstanceLockChip"));
    m_instanceChip->setAccessibleName(QStringLiteral("实例"));
    m_instanceChip->setToolTip(QStringLiteral("选择本会话关联的实例"));
    styleCapsule(
        m_instanceChip, 32, [] { return Theme::tokens().hover; }, [] { return Theme::tokens().subtle; },
        [] { return Theme::tokens().text; });
    {
        auto *row = new QHBoxLayout(m_instanceChip);
        row->setContentsMargins(12, 0, 12, 0);
        row->setSpacing(8);
        auto *lbl = new QLabel(QStringLiteral("实例"), m_instanceChip);
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        lbl->setFont(pxFont(lbl->font(), 13.5));
        m_instanceValue = new QLabel(QStringLiteral("无"), m_instanceChip);
        m_instanceValue->setObjectName(QStringLiteral("agentInstanceValue"));
        m_instanceValue->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_instanceValue->setFont(pxFont(m_instanceValue->font(), 13.5, QFont::Medium));
        const auto tint = [lbl, value = m_instanceValue] {
            setInk(lbl, Theme::tokens().secondary);
            setInk(value, Theme::tokens().text);
        };
        tint();
        Theme::follow(m_instanceChip, tint);
        row->addWidget(lbl);
        row->addWidget(m_instanceValue);
    }
    headerLayout->addWidget(m_instanceChip);

    m_moreButton = iconButton(header, QStringLiteral("agentMoreButton"), QStringLiteral("more"),
                              QStringLiteral("更多"), 36, 20);
    headerLayout->addWidget(m_moreButton);
    m_settingsButton = iconButton(header, QStringLiteral("agentApiSettingsButton"), QStringLiteral("gear"),
                                  QStringLiteral("API 设置"), 36, 19);
    headerLayout->addWidget(m_settingsButton);
    chatLayout->addWidget(header);

    // ⋯ 菜单（建一次、反复弹：加密开关这颗要常在，测试也按 objectName 找它）。
    {
        auto *menu = new PopMenu(this, QStringLiteral("agentMoreMenu"), 248);
        m_moreMenu = menu;
        m_renameAction = menu->addRow(QStringLiteral("agentRenameAction"), QStringLiteral("edit"), QStringLiteral("重命名"));
        m_pinAction = menu->addRow(QStringLiteral("agentPinAction"), QStringLiteral("star"), QStringLiteral("固定"));
        // 加密存储：一行（图标 + 字 + 开关）+ 下面一行小字说副作用。整行点哪儿都是拨开关。
        auto *encryptRow = menu->addRow(QStringLiteral("agentEncryptRow"), QStringLiteral("lock"), QStringLiteral("加密存储"));
        m_encryptCheck = new QCheckBox(QStringLiteral("加密存储"), encryptRow);
        m_encryptCheck->setObjectName(QStringLiteral("agentEncryptSessionCheck"));
        m_encryptCheck->setAccessibleName(QStringLiteral("加密存储"));
        Theme::markSwitch(m_encryptCheck);
        {
            auto *h = new QHBoxLayout(encryptRow);
            h->setContentsMargins(0, 0, 10, 0);
            h->addStretch(1);
            h->addWidget(m_encryptCheck);
        }
        connect(encryptRow, &QPushButton::clicked, m_encryptCheck, &QCheckBox::toggle);
        auto *encryptNote = new QLabel(QStringLiteral("标题和时间仍为明文。"), menu);
        encryptNote->setObjectName(QStringLiteral("agentEncryptNote"));
        encryptNote->setFont(pxFont(encryptNote->font(), 12.5));
        encryptNote->setContentsMargins(40, 0, 12, 6);
        setInk(encryptNote, Theme::tokens().secondary);
        Theme::follow(encryptNote, [encryptNote] { setInk(encryptNote, Theme::tokens().secondary); });
        menu->layout()->addWidget(encryptNote);
        m_exportAction = menu->addRow(QStringLiteral("agentExportAction"), QStringLiteral("export"), QStringLiteral("导出…"));
        menu->addDivider();
        m_deleteAction = menu->addRow(QStringLiteral("agentDeleteAction"), QStringLiteral("trash"),
                                      QStringLiteral("删除会话"), false, true);
        menu->installEventFilter(this);
        const auto closeThen = [this](std::function<void()> fn) {
            return [this, fn] {
                m_moreMenu->close();
                if (!m_currentSessionId.isEmpty()) fn();
            };
        };
        connect(m_renameAction, &QPushButton::clicked, this, closeThen([this] { promptRename(m_currentSessionId); }));
        connect(m_pinAction, &QPushButton::clicked, this, closeThen([this] {
                    emit sessionPinRequested(m_currentSessionId, !m_metaById.value(m_currentSessionId).pinned);
                }));
        connect(m_exportAction, &QPushButton::clicked, this,
                closeThen([this] { emit sessionExportRequested(m_currentSessionId); }));
        connect(m_deleteAction, &QPushButton::clicked, this,
                closeThen([this] { emit sessionDeleteRequested(m_currentSessionId); }));
    }

    // ---------- 对话流 ----------
    m_scroll = new QScrollArea(chat);
    m_scroll->setObjectName(QStringLiteral("agentMessageScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(m_scroll);
    host->setObjectName(QStringLiteral("agentStreamHost"));
    auto *hostRow = new QHBoxLayout(host);
    hostRow->setContentsMargins(0, 18, 0, 18);
    m_messageHost = new QWidget(host);
    m_messageHost->setObjectName(QStringLiteral("agentMessageHost"));
    m_messageHost->setMaximumWidth(kColumnMax);
    hostRow->addWidget(m_messageHost);
    m_messageLayout = new QVBoxLayout(m_messageHost);
    m_messageLayout->setContentsMargins(0, 0, 0, 0);
    m_messageLayout->setSpacing(18);

    // 空状态：居中一行大字（手写体）+ 一行小字；未配置 API 时多一颗主按钮。
    m_emptyState = new QWidget(m_messageHost);
    m_emptyState->setObjectName(QStringLiteral("agentEmptyState"));
    {
        auto *v = new QVBoxLayout(m_emptyState);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        v->addStretch(1);
        m_emptyTitle = new QLabel(m_emptyState);
        m_emptyTitle->setObjectName(QStringLiteral("agentEmptyTitle"));
        m_emptyTitle->setAlignment(Qt::AlignCenter);
        m_emptyTitle->setFont(Theme::handFont(pxFont(m_emptyTitle->font(), 24), Theme::HandFont::KaiLight));
        v->addWidget(m_emptyTitle);
        v->addSpacing(8);
        m_emptyText = new QLabel(m_emptyState);
        m_emptyText->setObjectName(QStringLiteral("agentEmptyText"));
        m_emptyText->setAlignment(Qt::AlignCenter);
        m_emptyText->setWordWrap(true);
        m_emptyText->setFont(pxFont(m_emptyText->font(), 13));
        m_emptyText->setForegroundRole(QPalette::PlaceholderText);
        v->addWidget(m_emptyText);
        m_emptyAction = new QPushButton(QStringLiteral("配置 API"), m_emptyState);
        m_emptyAction->setObjectName(QStringLiteral("agentEmptyConfigureButton"));
        m_emptyAction->setCursor(Qt::PointingHandCursor);
        Theme::markButton(m_emptyAction, Theme::ButtonKind::Primary);
        v->addSpacing(18);
        v->addWidget(m_emptyAction, 0, Qt::AlignHCenter);
        v->addStretch(1);
    }
    m_messageLayout->addWidget(m_emptyState, 1);

    // 系统提示（「已改回明文」「已停止生成」这类）：不是消息，居中一行淡字 + 两侧细线，跟在流的末尾。
    m_notice = new QWidget(m_messageHost);
    m_notice->setObjectName(QStringLiteral("agentNotice"));
    {
        auto *h = new QHBoxLayout(m_notice);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(12);
        h->addStretch(1);
        h->addWidget(new HairLine(m_notice), 1, Qt::AlignVCenter);
        m_noticeText = new QLabel(m_notice);
        m_noticeText->setObjectName(QStringLiteral("agentNoticeText"));
        m_noticeText->setWordWrap(true);
        m_noticeText->setAlignment(Qt::AlignCenter);
        m_noticeText->setFont(pxFont(m_noticeText->font(), 12.5));
        m_noticeText->setForegroundRole(QPalette::PlaceholderText);
        m_noticeText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_noticeText->setMaximumWidth(kColumnMax - 2 * 132);
        h->addWidget(m_noticeText);
        h->addWidget(new HairLine(m_notice), 1, Qt::AlignVCenter);
        h->addStretch(1);
    }
    m_notice->hide();
    m_messageLayout->addWidget(m_notice);
    m_messageLayout->addStretch(1);
    m_scroll->setWidget(host);
    chatLayout->addWidget(m_scroll, 1);

    // ---------- 输入岛 ----------
    auto *composerRow = new ComposerBox(chat);
    composerRow->setObjectName(QStringLiteral("agentComposerRow"));
    composerRow->setMaximumWidth(kColumnMax);
    auto *composerLayout = new QVBoxLayout(composerRow);
    composerLayout->setContentsMargins(18, 12, 10, 10);
    composerLayout->setSpacing(4);

    m_composer = new AgentComposer(composerRow);
    m_composer->setMinimumHeight(44);
    // 输入框默认 sizeHint 有 190 来高：压到约三行，长文自己滚。
    m_composer->setMaximumHeight(m_composer->fontMetrics().lineSpacing() * 3 + 14);
    m_composer->setFrameShape(QFrame::NoFrame);
    m_composer->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: none; padding: 0; }"));
    m_composer->viewport()->setAutoFillBackground(false);
    composerLayout->addWidget(m_composer, 1);

    auto *composerBar = new QHBoxLayout;
    composerBar->setContentsMargins(0, 0, 0, 0);
    composerBar->setSpacing(6);
    m_keyHint = new QLabel(QStringLiteral("Enter 发送 · Shift+Enter 换行"), composerRow);
    m_keyHint->setObjectName(QStringLiteral("agentComposerHint"));
    m_keyHint->setFont(pxFont(m_keyHint->font(), 12.5));
    m_keyHint->setForegroundRole(QPalette::PlaceholderText);
    composerBar->addWidget(m_keyHint);
    composerBar->addStretch(1);
    m_send = new QPushButton(composerRow);
    m_send->setObjectName(QStringLiteral("agentSendButton"));
    m_send->setDefault(true);
    m_send->setEnabled(false);
    markRoundButton(m_send, QStringLiteral("send"), QStringLiteral("发送"), Theme::ButtonKind::Primary);
    m_stop = new QPushButton(composerRow);
    m_stop->setObjectName(QStringLiteral("agentStopButton"));
    markRoundButton(m_stop, QStringLiteral("stop"), QStringLiteral("停止"), Theme::ButtonKind::Neutral);
    m_stop->setEnabled(false);
    m_stop->hide();
    composerBar->addWidget(m_send);
    composerBar->addWidget(m_stop);
    composerLayout->addLayout(composerBar);
    chatLayout->addWidget(composerRow, 0, Qt::AlignHCenter);
    // 岛宽跟对话列一起走：不到 760 时撑满，超过时居中定宽。
    composerRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(chat, 1);

    // ---------- 接线 ----------
    connect(m_newSession, &QPushButton::clicked, this, &AgentPage::newSessionRequested);
    connect(m_settingsButton, &QPushButton::clicked, this, &AgentPage::settingsRequested);
    connect(m_apiWarn, &QPushButton::clicked, this, &AgentPage::settingsRequested);
    connect(m_emptyAction, &QPushButton::clicked, this, &AgentPage::settingsRequested);
    connect(m_instanceChip, &QPushButton::clicked, this, &AgentPage::instanceChipRequested);
    connect(m_moreButton, &QPushButton::clicked, this, &AgentPage::openMoreMenu);
    connect(m_send, &QPushButton::clicked, this, [this] {
        if (m_configured) emit sendRequested(m_composer->toPlainText());
    });
    connect(m_stop, &QPushButton::clicked, this, &AgentPage::stopRequested);
    connect(m_composer, &AgentComposer::sendRequested, this, [this] {
        if (m_configured && !m_streaming) emit sendRequested(m_composer->toPlainText());
    });
    connect(m_composer, &QPlainTextEdit::textChanged, this, [this] {
        updateSendEnabled();
        updateComposerHint();
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
    connect(m_encryptCheck, &QCheckBox::toggled, this, &AgentPage::encryptToggled);
    setConfigured(false, QStringLiteral("API 未配置"));
}

int AgentPage::sidebarWidth() { return 236; }
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
    const QString title = meta.title.isEmpty() ? QStringLiteral("新对话") : meta.title;
    item->setText(title);
    item->setData(kTimeRole, relativeTime(meta.updatedAt, QDateTime::currentDateTimeUtc()));
    item->setData(Qt::UserRole, meta.id);
    item->setData(Qt::UserRole + 1, meta.pinned);
    item->setToolTip(title);
    item->setData(Qt::AccessibleTextRole, title);
}

void AgentPage::setSessions(const QList<AgentView::SessionMeta> &sessions) {
    m_metaById.clear();
    m_sessionList->clear();
    // 分三组：固定 / 今天 / 更早。组内保持传进来的先后；空组不出小标题。
    const QDate today = QDate::currentDate();
    QList<AgentView::SessionMeta> groups[3];
    for (const AgentView::SessionMeta &meta : sessions) {
        const int g = meta.pinned ? 0 : (meta.updatedAt.toLocalTime().date() == today ? 1 : 2);
        groups[g].append(meta);
    }
    const QString titles[3] = {QStringLiteral("固定"), QStringLiteral("今天"), QStringLiteral("更早")};
    for (int g = 0; g < 3; ++g) {
        if (groups[g].isEmpty()) continue;
        auto *header = new QListWidgetItem(titles[g]);
        header->setFlags(Qt::NoItemFlags);
        header->setData(kGroupHeaderRole, true);
        m_sessionList->addItem(header);
        for (const AgentView::SessionMeta &meta : std::as_const(groups[g])) {
            m_metaById.insert(meta.id, meta);
            auto *item = new QListWidgetItem;
            rebuildSessionRow(item, meta);
            m_sessionList->addItem(item);
        }
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
            setEncryptionChecked(meta.encrypted);
            m_title->setText(meta.title);
        }
    }
    if (!m_search->text().isEmpty()) emit m_search->textChanged(m_search->text());
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
    m_title->setText(meta.isValid() ? meta.title : QStringLiteral("新对话"));
    setEncryptionChecked(meta.isValid() && meta.encrypted);
    showNotice(QString());
}

void AgentPage::setTitle(const QString &title) {
    m_title->setText(title.isEmpty() ? QStringLiteral("新对话") : title);
}

void AgentPage::insertStreamWidget(QWidget *widget) {
    // 流的顺序：空状态 · 消息 / 工具卡 / 审批卡 … · 系统提示 · 弹簧。新来的插在系统提示前面。
    m_messageLayout->insertWidget(m_messageLayout->indexOf(m_notice), widget);
}

void AgentPage::setMessages(const QList<AgentView::Message> &messages) {
    clearMessages();
    for (const AgentView::Message &message : messages) {
        upsertMessage(message);
    }
    updateEmptyState();
    scrollToLatest();
}

void AgentPage::upsertMessage(const AgentView::Message &message) {
    AgentMessageView *view = viewFor(message.id);
    if (view == nullptr) {
        view = new AgentMessageView(message, m_messageHost);
        connect(view, &AgentMessageView::retryRequested, this, &AgentPage::retryRequested);
        m_views.insert(message.id, view);
        insertStreamWidget(view);
        // 自己说的话靠右一块淡底气泡，助手的话铺满（样张 .msg.me / .msg.ai）。
        if (message.user) m_messageLayout->setAlignment(view, Qt::AlignRight);
    } else {
        view->setMessage(message);
    }
    updateEmptyState();
    scrollToLatest();
}

void AgentPage::removeMessage(const QString &messageId) {
    AgentMessageView *view = m_views.take(messageId);
    if (view == nullptr) return;
    m_messageLayout->removeWidget(view);
    view->deleteLater();
    updateEmptyState();
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
    updateEmptyState();
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
        insertStreamWidget(view);
    } else {
        view->setCard(card);
    }
    updateEmptyState();
    scrollToLatest();
}

void AgentPage::clearToolCards() {
    const auto cards = m_toolCards;
    m_toolCards.clear();
    for (AgentToolCard *card : cards) {
        m_messageLayout->removeWidget(card);
        card->deleteLater();
    }
    updateEmptyState();
}

void AgentPage::showApproval(const QJsonObject &request) {
    if (m_approvalCard == nullptr) {
        m_approvalCard = new AgentApprovalCard(m_messageHost);
        connect(m_approvalCard, &AgentApprovalCard::allowed, this,
                &AgentPage::approvalAllowed);
        connect(m_approvalCard, &AgentApprovalCard::denied, this,
                &AgentPage::approvalDenied);
        insertStreamWidget(m_approvalCard);
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
    if (!statusText.isEmpty()) m_statusText = statusText;
    // 未配置：顶上一颗警示胶囊（点了开 API 设置），空状态换成「先配置 API」，输入岛停用。
    m_apiWarn->setVisible(!configured);
    m_apiWarn->setText(m_statusText.startsWith(QStringLiteral("配置无效")) ? QStringLiteral("API 配置无效")
                                                                        : QStringLiteral("API 未配置"));
    m_apiWarn->setToolTip(m_statusText);
    m_composer->setReadOnly(!configured);
    setModelLabel(m_modelLabel);
    updateEmptyState();
    updateComposerHint();
    updateSendEnabled();
}

void AgentPage::setModelLabel(const QString &label) {
    m_modelLabel = label;
    QStringList tip{QStringLiteral("API 设置")};
    if (m_configured && !label.isEmpty()) tip << label;
    m_settingsButton->setToolTip(tip.join(QStringLiteral(" · ")));
}

void AgentPage::setInstanceChip(const QString &text, bool locked) {
    m_instanceValue->setText(text);
    m_instanceChip->setProperty("locked", locked);
}

void AgentPage::showInstanceMenu(const QList<AgentView::InstanceOption> &instances, const QString &lockedId) {
    if (m_instanceMenu != nullptr) m_instanceMenu->deleteLater();
    auto *menu = new PopMenu(this, QStringLiteral("agentInstanceMenu"), 260);
    m_instanceMenu = menu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    connect(menu, &QObject::destroyed, this, [this, menu] {
        if (m_instanceMenu == menu) m_instanceMenu = nullptr;
        markOpen(m_instanceChip, false);
    });
    const auto pick = [this, menu](const QString &id) {
        return [this, menu, id] {
            menu->close();
            emit instanceChosen(id);
        };
    };
    QPushButton *follow = menu->addRow(QStringLiteral("agentInstanceFollowHome"), QStringLiteral("home"),
                                       QStringLiteral("跟随主页"), lockedId.isEmpty());
    connect(follow, &QPushButton::clicked, this, pick(QString()));
    if (!instances.isEmpty()) menu->addHead(QStringLiteral("实例"));
    for (int i = 0; i < instances.size(); ++i) {
        const AgentView::InstanceOption &option = instances.at(i);
        QPushButton *row = menu->addRow(QStringLiteral("agentInstanceOption%1").arg(i), QString(), option.name,
                                        option.id == lockedId);
        row->setToolTip(option.name);
        connect(row, &QPushButton::clicked, this, pick(option.id));
    }
    markOpen(m_instanceChip, true);
    menu->popBelow(m_instanceChip);
}

void AgentPage::openMoreMenu() {
    const bool has = !m_currentSessionId.isEmpty();
    for (QPushButton *b : {m_renameAction, m_pinAction, m_exportAction, m_deleteAction}) b->setEnabled(has);
    m_encryptCheck->parentWidget()->setEnabled(has);
    m_pinAction->setText(m_metaById.value(m_currentSessionId).pinned ? QStringLiteral("取消固定")
                                                                     : QStringLiteral("固定"));
    markOpen(m_moreButton, true);
    static_cast<PopMenu *>(m_moreMenu)->popBelow(m_moreButton);
}

void AgentPage::setEncryptionChecked(bool checked) {
    QSignalBlocker blocker(m_encryptCheck);
    m_encryptCheck->setChecked(checked);
}

void AgentPage::showNotice(const QString &text) {
    m_noticeText->setText(text);
    m_notice->setVisible(!text.isEmpty());
    if (!text.isEmpty()) scrollToLatest();
}

void AgentPage::showEmptyState(bool empty) { m_emptyState->setVisible(empty); }

void AgentPage::updateEmptyState() {
    const bool empty = m_views.isEmpty() && m_toolCards.isEmpty() && m_approvalCard == nullptr;
    m_emptyState->setVisible(empty);
    m_emptyTitle->setText(m_configured ? QStringLiteral("有什么想问的？") : QStringLiteral("先配置 API"));
    m_emptyText->setText(m_configured ? QStringLiteral("可查看关联实例的文件；写入文件前会先征得确认。")
                                      : QStringLiteral("填写服务地址和密钥后即可开始对话。"));
    m_emptyAction->setVisible(!m_configured);
    if (!m_configured) m_composer->setPlaceholderText(QStringLiteral("配置 API 后可输入"));
    else m_composer->setPlaceholderText(empty ? QStringLiteral("描述你遇到的问题…") : QStringLiteral("继续追问…"));
}

void AgentPage::updateComposerHint() {
    // 「Enter 发送」只在输入框空着时露出来（样张 .composer），未配置时也收起。
    m_keyHint->setVisible(m_configured && m_composer->toPlainText().isEmpty());
}

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
    // 窗口窄于 1024（导航轨收成 64 的那一档）侧栏收到 216（tokens-r2 §8）。
    const int width = window()->width() < 1024 ? sidebarNarrowWidth() : sidebarWidth();
    if (m_sidebar->width() != width) m_sidebar->setFixedWidth(width);
    const int pad = event->size().width() - width < 700 ? 16 : 0;
    if (auto *chatLayout = qobject_cast<QVBoxLayout *>(m_scroll->parentWidget()->layout()))
        chatLayout->setContentsMargins(pad ? 16 : 24, 12, pad ? 16 : 20, pad ? 16 : 20);
}

bool AgentPage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_title && event->type() == QEvent::MouseButtonDblClick && !m_currentSessionId.isEmpty()) {
        promptRename(m_currentSessionId);
        return true;
    }
    if (watched == m_moreMenu && event->type() == QEvent::Hide) markOpen(m_moreButton, false);
    return ThemeBackground::eventFilter(watched, event);
}

void AgentPage::promptRename(const QString &sessionId) {
    const AgentView::SessionMeta meta = m_metaById.value(sessionId);
    if (!meta.isValid()) return;
    // 问名字用内容区里那张提示卡，不另开带系统标题栏的 QInputDialog。本页挂在主窗的页签里，
    // 覆盖层是页签旁边那一个（centralWidget 的直接子控件），所以往上逐层找直接子控件。
    OverlayHost *host = OverlayHost::forWidget(this);
    for (QWidget *w = parentWidget(); host == nullptr && w != nullptr; w = w->parentWidget())
        host = w->findChild<OverlayHost *>(QString(), Qt::FindDirectChildrenOnly);
    bool accepted = false;
    const QString title = InlineMessage::getText(host, QStringLiteral("重命名会话"),
                                                 QStringLiteral("标题"), meta.title, &accepted);
    if (accepted) emit sessionRenameRequested(sessionId, title);
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
    QAction *exportAction = menu.addAction(QStringLiteral("导出…"));
    menu.addSeparator();
    QAction *remove = menu.addAction(QStringLiteral("删除"));
    QAction *chosen = menu.exec(m_sessionList->viewport()->mapToGlobal(position));
    if (chosen == rename) {
        promptRename(id);
    } else if (chosen == pin) {
        emit sessionPinRequested(id, !meta.pinned);
    } else if (chosen == exportAction) {
        emit sessionExportRequested(id);
    } else if (chosen == remove) {
        emit sessionDeleteRequested(id);
    }
}
