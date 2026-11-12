/**
 * @file AccountSkinDialog.cpp
 * @brief 「账号与外观」对话框（见 .hpp 里那几段说明）。
 */

#include "AccountSkinDialog.hpp"

// WheelGuard 住在 MainWindowUI.hpp 里（和 InstanceSettingsDialog.cpp 一样，直接引那一份，
// 不另外抄一个防护类）。它护的是下面那个「模型」下拉框。
#include "MainWindowUI.hpp"
#include "RoundAvatar.hpp"
#include "SkinModel.hpp"
#include "Theme.hpp"   // 这一窗里「删除所选」那一颗是危险按钮（红字红边，见构造函数）
#include "SkinViewer.hpp"
#include "OverlayHost.hpp"
#include "VersionIcons.hpp"
#include <QShowEvent>
#include <QResizeEvent>

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
constexpr int kInitialWidth = 980;
constexpr int kInitialHeight = 460;
constexpr int kAccountRowHeight = 58;   // 设计稿 .acct-row：头像 32 + 上下 13
constexpr int kAccountsWidth = 372;
constexpr int kControlsWidth = 280;
constexpr int kCapeCell = 44;            // 设计稿 .cape：44 × 60
constexpr int kCapeCellHeight = 60;
constexpr int kCapeGap = 8;

// 行里显示用的拆开那几样（条目 text() 仍是「名字（离线）（当前）」那一整句，测试和无障碍读它）。
constexpr int kNameRole = Qt::UserRole + 2;
constexpr int kOfflineRole = Qt::UserRole + 3;
constexpr int kSavedRole = Qt::UserRole + 4;

/// 去掉「（当前）」「（离线）」两头的括号，拿来做徽标和副标题（同一份文案，不另起一句）。
QString bare(const QString &mark) { return mark.mid(1, mark.size() - 2); }

QFont pixelFont(QFont font, int px, QFont::Weight weight = QFont::Normal) {
    font.setPixelSize(px);
    font.setWeight(weight);
    return font;
}

/// 账号行（设计稿 .acct-row）：头像 32 圆角 8、名字 14 + 「当前」小标、下面一行登录方式。
class AccountRowDelegate : public QStyledItemDelegate {
public:
    AccountRowDelegate(QObject *parent, const QImage *currentSkin)
        : QStyledItemDelegate(parent), m_currentSkin(currentSkin) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.data(kNameRole).isNull()) return QStyledItemDelegate::sizeHint(option, index);
        return QSize(option.rect.width(), kAccountRowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        if (index.data(kNameRole).isNull()) {  // 「还没有保存的账号。」那一行
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const Theme::Tokens t = Theme::tokens();
        const QString name = index.data(kNameRole).toString();
        const bool current = index.data(Qt::UserRole + 1).toBool();
        const bool offline = index.data(kOfflineRole).toBool();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRectF r = QRectF(option.rect).adjusted(0, 1, 0, -1);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        if (selected || hovered) {
            painter->setBrush(selected ? t.selected : t.hover);
            painter->drawRoundedRect(r, 12, 12);
        }
        if (selected) {
            painter->setBrush(t.primary);
            painter->drawRoundedRect(QRectF(r.left() + 2, r.center().y() - 10, 3, 20), 1.5, 1.5);
        }

        const QRect avatar(int(r.left()) + 12, int(r.center().y()) - 16, 32, 32);
        const QImage face = current && !m_currentSkin->isNull()
                                ? SkinModel::headAvatar(*m_currentSkin, 64)
                                : SkinModel::letterAvatar(name, 64);
        // 圆头像：和主页右上那颗、账号下拉面板里那几行同一种形状。
        RoundAvatar::paint(*painter, QRectF(avatar), face);

        const int textLeft = avatar.right() + 13;
        // 选中那一行行尾压着「切换 / 删除」两颗（placeRowActions()），字给它们让位。
        const int actions = selected ? option.widget->property("wtfmlRowActionsWidth").toInt() : 0;
        const int textRight = int(r.right()) - (current && !selected ? 40 : 16) - actions;
        const QFont nameFont = pixelFont(option.font, 14, QFont::DemiBold);
        const QFont subFont = pixelFont(option.font, 12);
        const QFontMetrics nm(nameFont);
        const QFontMetrics sm(subFont);
        const int blockTop = int(r.center().y()) - (nm.height() + 2 + sm.height()) / 2;

        painter->setFont(nameFont);
        painter->setPen(t.text);
        const int badgeRoom = current ? 52 : 0;
        const QString shownName = nm.elidedText(name, Qt::ElideRight, qMax(0, textRight - textLeft - badgeRoom));
        painter->drawText(QRect(textLeft, blockTop, textRight - textLeft, nm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, shownName);
        if (current) {
            const QFont badgeFont = pixelFont(option.font, 11, QFont::DemiBold);
            const QString badge = bare(AccountSkinDialog::currentMark());
            const int bw = QFontMetrics(badgeFont).horizontalAdvance(badge) + 16;
            const QRectF pill(textLeft + nm.horizontalAdvance(shownName) + 8,
                              blockTop + (nm.height() - 18) / 2.0, bw, 18);
            painter->setPen(Qt::NoPen);
            painter->setBrush(t.accentSoft2);
            painter->drawRoundedRect(pill, 9, 9);
            painter->setFont(badgeFont);
            painter->setPen(Theme::textTone(t.accentInk, t.primary));
            painter->drawText(pill, Qt::AlignCenter, badge);
        }

        QString sub = offline ? bare(AccountSkinDialog::offlineMark())
                              : MainWindowUI::accountTypeOnlineText();
        const QDateTime savedAt = QDateTime::fromString(index.data(kSavedRole).toString(), Qt::ISODate);
        if (!offline && savedAt.isValid()) {
            sub += QStringLiteral(" · ") +
                   QStringLiteral("登录取自 %1").arg(savedAt.toLocalTime().toString(QStringLiteral("M 月 d 日")));
        }
        painter->setFont(subFont);
        painter->setPen(t.secondary);
        painter->drawText(QRect(textLeft, blockTop + nm.height() + 2, textRight - textLeft, sm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, sm.elidedText(sub, Qt::ElideRight, textRight - textLeft));

        if (current && !selected) {
            VersionIcons::paintLine(*painter, QStringLiteral("check"),
                                    QRectF(r.right() - 32, r.center().y() - 8, 16, 16), t.secondary);
        }
        painter->restore();
    }

private:
    const QImage *m_currentSkin;
};

/// 披风格（设计稿 .cape）：44 × 60 圆角 10，中间一块按 id 稳定配色的竖条；选中那格描主题色。
class CapeCellDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return QSize(kCapeCell + kCapeGap, kCapeCellHeight + kCapeGap);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const Theme::Tokens t = Theme::tokens();
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        const QRectF cell(option.rect.left() + kCapeGap / 2.0, option.rect.top() + kCapeGap / 2.0,
                          kCapeCell, kCapeCellHeight);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QColor fill = t.text;
        fill.setAlpha(hovered ? 26 : 16);
        painter->setPen(selected ? QPen(t.primary, 2) : QPen(t.glassEdge, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        const QImage texture = index.data(Qt::UserRole + 3).value<QImage>();
        painter->setPen(Qt::NoPen);
        if (!texture.isNull() && texture.width() >= 64) {
            // 披风贴图正面在 (1,1) 起 10×16（按 64 宽的比例换算高清贴图），放大 2 倍、最近邻不糊。
            const int unit = texture.width() / 64;
            const QRectF front(cell.center().x() - 10, cell.center().y() - 16, 20, 32);
            painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter->drawImage(front, texture, QRect(unit, unit, 10 * unit, 16 * unit));
        } else {
            // 贴图还没到（或没地址）：按 id 取一个稳定色块占位。
            QColor swatch = SkinModel::avatarFallbackColor(index.data(Qt::UserRole).toString());
            painter->setBrush(swatch);
            painter->drawRoundedRect(QRectF(cell.center().x() - 8, cell.top() + 14, 16, 32), 3, 3);
        }
        if (index.data(Qt::UserRole + 1).toBool()) {  // 正披着的那件：右上角一个小圆点
            painter->setBrush(t.ok);
            painter->drawEllipse(QPointF(cell.right() - 8, cell.top() + 8), 3, 3);
        }
        painter->restore();
    }
};

/// 侧栏式列表：无框、透明底，行自己画底色。
void makeListTransparent(QListWidget *list) {
    list->setFrameShape(QFrame::NoFrame);
    list->setMouseTracking(true);
    list->setAutoFillBackground(false);
    list->viewport()->setAutoFillBackground(false);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QLabel *makeSectionTitle(const QString &text, QWidget *parent, int px) {
    auto *label = new QLabel(text, parent);
    QFont font = Theme::handFont(label->font(), Theme::HandFont::Kai);
    font.setPixelSize(px);
    label->setFont(font);
    return label;
}

QLabel *makeCaption(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setFont(Theme::textFont(label->font(), Theme::TextRole::Caption));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}
}  // namespace

AccountSkinDialog::AccountSkinDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(windowTitle());
    setObjectName(QStringLiteral("accountSkinDialog"));
    // 尺寸**不**跟着内容长：这个窗里的东西是固定的三块，摆好之后让用户自己拉。
    resize(kInitialWidth, kInitialHeight);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(12);
    root->setAlignment(Qt::AlignTop);

    // ---------- 顶部（设计稿 .page-head）：大标题 + 一句身份副标题，「刷新外观」在右 ----------
    m_headerBar = new QWidget(this);
    m_headerBar->setObjectName(QStringLiteral("accountHeaderBar"));
    m_headerBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *statusRow = new QHBoxLayout(m_headerBar);
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->setSpacing(12);
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(2);
    auto *title = new QLabel(windowTitle(), m_headerBar);
    title->setObjectName(QStringLiteral("accountPageTitle"));
    {
        QFont f = Theme::handFont(title->font(), Theme::HandFont::KaiLight);
        f.setPixelSize(Theme::textSize(Theme::TextRole::Display));
        title->setFont(f);
    }
    m_statusLabel = new QLabel(notLoggedInNote(), m_headerBar);
    m_statusLabel->setObjectName(statusLabelName());
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setMaximumWidth(480);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_statusLabel->setFont(Theme::textFont(m_statusLabel->font(), Theme::TextRole::Caption));
    const auto tintStatus = [label = m_statusLabel] {
        label->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::tokens().secondary.name()));
    };
    tintStatus();
    Theme::follow(m_statusLabel, tintStatus);
    titleColumn->addWidget(title);
    titleColumn->addWidget(m_statusLabel);
    statusRow->addLayout(titleColumn, 1);
    root->addWidget(m_headerBar);

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("accountBody"));
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QHBoxLayout *main = new QHBoxLayout(body);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(28);
    m_bodyLayout = main;
    root->addWidget(body);

    // ---------- 左：账号（设计稿 .acct-col）----------
    // 标题 → 账号列表 → 「登录新账号…」 → 说明 → 「或添加离线账号」 → 离线那一行。
    m_accountsPanel = new QWidget(this);
    m_accountsPanel->setObjectName(QStringLiteral("accountManagementPane"));
    m_accountsPanel->setMaximumWidth(kAccountsWidth);
    // 和立绘两边那两段 stretch 抢宽度时要占上风（Preferred 会被 stretch 压到最小宽）。
    m_accountsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout *left = new QVBoxLayout(m_accountsPanel);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(10);
    left->setAlignment(Qt::AlignTop);
    // 精修：伸缩权重压过立绘两边那两段 stretch（各 1），别被压到 240 上下（名字只剩一个
    // 字母、行尾按钮挤成一团）。不用最小宽度：那会把整窗最小宽顶过窄窗阈值、再也叠不成上下。
    main->addWidget(m_accountsPanel, 8);

    {
        // 设计稿 acct-col：「账号」后面跟一截灰字「正版 / 离线」，不再写成括号。
        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(10);
        titleRow->addWidget(makeSectionTitle(accountSectionTitle(), this, 20), 0, Qt::AlignBottom);
        titleRow->addWidget(makeCaption(QStringLiteral("正版 / 离线"), this), 0, Qt::AlignBottom);
        titleRow->addStretch(1);
        left->addLayout(titleRow);
    }

    m_accountList = new QListWidget(this);
    m_accountList->setObjectName(accountListName());
    makeListTransparent(m_accountList);
    m_accountList->setItemDelegate(new AccountRowDelegate(m_accountList, &m_currentSkin));
    m_accountList->viewport()->installEventFilter(this);
    left->addWidget(m_accountList);

    // 「切换到所选」「删除所选」：不再占一整排，跟着选中那一行摆在行尾（placeRowActions()）。
    m_rowActions = new QWidget(m_accountList->viewport());
    auto *rowActionsLayout = new QHBoxLayout(m_rowActions);
    rowActionsLayout->setContentsMargins(0, 0, 0, 0);
    rowActionsLayout->setSpacing(4);
    m_switchButton = new QPushButton(switchButtonText(), m_rowActions);
    m_switchButton->setObjectName(switchButtonName());
    // 「用这个账号」= 换个身份（一般性那一档）。
    Theme::markButton(m_switchButton, Theme::ButtonKind::Ghost);
    m_removeButton = new QPushButton(removeButtonText(), m_rowActions);
    m_removeButton->setObjectName(removeButtonName());
    // 「删除所选」是这一窗里唯一一处**删东西**的按钮（账号删了就没了），所以它走那一档
    // **红字**（`Theme::ButtonKind::Danger`）。属性要在第一次 polish 之前挂上。
    Theme::markButton(m_removeButton, Theme::ButtonKind::Danger);
    rowActionsLayout->addWidget(m_switchButton);
    rowActionsLayout->addWidget(m_removeButton);
    connect(m_accountList->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &AccountSkinDialog::placeRowActions);

    m_loginButton = new QPushButton(loginButtonText(), this);
    m_loginButton->setObjectName(loginButtonName());
    m_loginButton->setToolTip(loginHintNote());
    Theme::markButton(m_loginButton, Theme::ButtonKind::Primary);
    m_loginButton->setProperty("wtfmlDialogActions", true);
    left->addSpacing(4);
    left->addWidget(m_loginButton);
    left->addWidget(makeCaption(loginHintNote(), this));

    // 「或添加离线账号」：两边各一根细线（设计稿 .or）。
    {
        auto *orRow = new QHBoxLayout();
        orRow->setSpacing(10);
        const auto rule = [this] {
            auto *line = new QFrame(this);
            line->setFrameShape(QFrame::HLine);
            line->setFixedHeight(1);
            const auto tint = [line] {
                QPalette palette = line->palette();
                palette.setColor(QPalette::WindowText, Theme::tokens().divider);
                line->setPalette(palette);
            };
            tint();
            Theme::follow(line, tint);
            return line;
        };
        orRow->addWidget(rule(), 1);
        orRow->addWidget(makeCaption(QStringLiteral("或") + addOfflineButtonText(), this));
        orRow->addWidget(rule(), 1);
        left->addLayout(orRow);
    }

    // 离线账号那一路：不是对"选中项"做事，是一个"加东西"的入口。
    QHBoxLayout *offlineRow = new QHBoxLayout();
    offlineRow->setSpacing(8);
    left->addLayout(offlineRow);
    m_offlineNameEdit = new QLineEdit(this);
    m_offlineNameEdit->setObjectName(offlineNameEditName());
    m_offlineNameEdit->setPlaceholderText(offlineNamePlaceholder());
    m_addOfflineButton = new QPushButton(addOfflineButtonText(), this);
    m_addOfflineButton->setObjectName(addOfflineButtonName());
    Theme::markButton(m_addOfflineButton, Theme::ButtonKind::Neutral);
    offlineRow->addWidget(m_offlineNameEdit, 1);
    offlineRow->addWidget(m_addOfflineButton);

    m_refreshButton = new QPushButton(refreshButtonText(), this);
    m_refreshButton->setObjectName(refreshButtonName());
    // 「刷新外观」刷的是**这一整个窗**（账号列表 + 皮肤 + 披风），所以放页头，不放某一栏里。
    Theme::markButton(m_refreshButton, Theme::ButtonKind::Neutral);
    {  // 图标颜色跟主题走（构造时上色的话换主题后还是上一套的字色）
        const auto tint = [b = m_refreshButton] { b->setIcon(VersionIcons::line(QStringLiteral("refresh"), 16, Theme::tokens().text)); };
        tint();
        Theme::follow(m_refreshButton, tint);
    }
    statusRow->addStretch(1);  // 标题列有最大宽，余下的空白留在中间，别让按钮吃成一整条
    statusRow->addWidget(m_refreshButton, 0, Qt::AlignTop);

    // ---------- 中：立绘（设计稿 .stage）----------
    m_viewer = new SkinViewer(this);
    m_viewer->setObjectName(viewerName());
    m_viewer->setFixedSize(208, 312);
    m_viewer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // 一进来就有东西可看：没登录、还没取到、取不到，看到的都是这套默认外观。
    m_viewer->setSkin(SkinModel::defaultSteveSkin(), QImage(), false);
    main->addStretch(1);
    main->addWidget(m_viewer, 0, Qt::AlignTop);
    main->addStretch(1);

    // ---------- 右：皮肤 / 披风（设计稿 .looks）----------
    auto *controlsPanel = new QWidget(this);
    controlsPanel->setObjectName(QStringLiteral("accountLooksPane"));
    controlsPanel->setFixedWidth(kControlsWidth);
    auto *controls = new QVBoxLayout(controlsPanel);
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(10);
    controls->setAlignment(Qt::AlignTop);
    main->addWidget(controlsPanel, 0, Qt::AlignTop);

    controls->addWidget(makeSectionTitle(tr("皮肤"), this, 20));
    m_variantCombo = new QComboBox(this);
    m_variantCombo->setObjectName(variantComboName());
    m_variantCombo->addItem(variantClassicText(), variantClassicValue());
    m_variantCombo->addItem(variantSlimText(), variantSlimValue());
    // 滚轮防护：鼠标划过会静默换成另一种模型，接着点「上传」传上去的就是错的那种。
    WheelGuard::install(m_variantCombo);
    controls->addWidget(m_variantCombo);

    auto *skinButtons = new QHBoxLayout();
    skinButtons->setContentsMargins(0, 0, 0, 0);
    skinButtons->setSpacing(8);
    m_uploadButton = new QPushButton(uploadButtonText(), this);
    m_uploadButton->setObjectName(uploadButtonName());
    {  // 图标颜色跟主题走（构造时上色的话换主题后还是上一套的字色）
        const auto tint = [b = m_uploadButton] { b->setIcon(VersionIcons::line(QStringLiteral("upload"), 16, Theme::tokens().text)); };
        tint();
        Theme::follow(m_uploadButton, tint);
    }
    Theme::markButton(m_uploadButton, Theme::ButtonKind::Neutral);
    skinButtons->addWidget(m_uploadButton);

    // ---------- 离线账号那一档：换一张 / 恢复默认 ----------
    // 和「上传皮肤…」互斥（同一行里只露一档，见 setOfflineSkinState()）：离线账号没有令牌，
    // 上传按下去只会失败；它要的是"从盘上挑一张"。默认藏着。
    m_offlineSkinButton = new QPushButton(offlineSkinButtonText(), this);
    m_offlineSkinButton->setObjectName(offlineSkinButtonName());
    m_offlineSkinButton->setAutoDefault(false);
    m_offlineSkinButton->setToolTip(offlineSkinButtonToolTip());
    {  // 图标颜色跟主题走（构造时上色的话换主题后还是上一套的字色）
        const auto tint = [b = m_offlineSkinButton] { b->setIcon(VersionIcons::line(QStringLiteral("upload"), 16, Theme::tokens().text)); };
        tint();
        Theme::follow(m_offlineSkinButton, tint);
    }
    Theme::markButton(m_offlineSkinButton, Theme::ButtonKind::Neutral);
    m_offlineSkinResetButton = new QPushButton(offlineSkinResetButtonText(), this);
    m_offlineSkinResetButton->setObjectName(offlineSkinResetButtonName());
    m_offlineSkinResetButton->setAutoDefault(false);
    m_offlineSkinResetButton->setToolTip(offlineSkinResetButtonToolTip());
    // 「恢复默认外观」= 撤掉一层（和「不显示披风」同一档）。
    Theme::markButton(m_offlineSkinResetButton, Theme::ButtonKind::Ghost);
    skinButtons->addWidget(m_offlineSkinButton);
    skinButtons->addWidget(m_offlineSkinResetButton);
    skinButtons->addStretch(1);
    controls->addLayout(skinButtons);
    m_offlineSkinButton->hide();
    m_offlineSkinResetButton->hide();

    controls->addSpacing(10);
    controls->addWidget(makeSectionTitle(capeSectionTitle(), this, 20));

    m_capeList = new QListWidget(this);
    m_capeList->setObjectName(capeListName());
    makeListTransparent(m_capeList);
    m_capeList->setViewMode(QListView::IconMode);
    m_capeList->setFlow(QListView::LeftToRight);
    m_capeList->setWrapping(true);
    m_capeList->setResizeMode(QListView::Adjust);
    m_capeList->setMovement(QListView::Static);
    m_capeList->setSpacing(0);
    m_capeList->setItemDelegate(new CapeCellDelegate(m_capeList));
    m_capeList->setFixedHeight(kCapeCellHeight + kCapeGap + 4);
    m_capeList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    controls->addWidget(m_capeList);
    m_capeList->hide();  // 没有披风时不留一块空格子（setCapes() 按有没有再露出来）
    m_capeCaption = makeCaption(QString(), this);
    controls->addWidget(m_capeCaption);

    QHBoxLayout *capeButtons = new QHBoxLayout();
    capeButtons->setSpacing(8);
    controls->addLayout(capeButtons);
    m_applyCapeButton = new QPushButton(applyCapeButtonText(), this);
    m_applyCapeButton->setObjectName(applyCapeButtonName());
    m_applyCapeButton->setText(tr("应用"));
    m_applyCapeButton->setToolTip(applyCapeButtonText());
    Theme::markButton(m_applyCapeButton, Theme::ButtonKind::Primary);
    m_hideCapeButton = new QPushButton(hideCapeButtonText(), this);
    m_hideCapeButton->setObjectName(hideCapeButtonName());
    m_hideCapeButton->setText(tr("隐藏"));
    m_hideCapeButton->setToolTip(hideCapeButtonText());
    // 「不显示披风」= 拿掉一层；旁边那颗「应用披风」是确认性。
    Theme::markButton(m_hideCapeButton, Theme::ButtonKind::Neutral);
    capeButtons->addWidget(m_applyCapeButton);
    capeButtons->addWidget(m_hideCapeButton);
    capeButtons->addStretch(1);

    // ---------- 底：关闭 ----------
    // NOTE: 只有「关闭」，没有「确定」—— 这个窗里的每一件事（切换、上传、换披风）**当场
    // 就生效**，没有"确定之后才写盘"这一层。
    QHBoxLayout *bottom = new QHBoxLayout();
    root->addLayout(bottom);
    bottom->addStretch(1);
    m_closeButton = new QPushButton(closeButtonText(), this);
    m_closeButton->setObjectName(closeButtonName());
    Theme::markButton(m_closeButton, Theme::ButtonKind::Neutral);
    bottom->addWidget(m_closeButton);

    // ---------- 接线 ----------
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_loginButton, &QPushButton::clicked, this, &AccountSkinDialog::loginRequested);
    connect(m_addOfflineButton, &QPushButton::clicked, this,
            &AccountSkinDialog::submitOfflineName);
    // 在框里直接回车 = 点那个按钮。
    connect(m_offlineNameEdit, &QLineEdit::returnPressed, this,
            &AccountSkinDialog::submitOfflineName);
    connect(m_refreshButton, &QPushButton::clicked, this, &AccountSkinDialog::refreshRequested);
    connect(m_uploadButton, &QPushButton::clicked, this, &AccountSkinDialog::pickSkinFile);
    connect(m_offlineSkinButton, &QPushButton::clicked, this,
            &AccountSkinDialog::pickOfflineSkinFile);
    connect(m_offlineSkinResetButton, &QPushButton::clicked, this,
            &AccountSkinDialog::offlineSkinResetRequested);
    connect(m_accountList, &QListWidget::itemSelectionChanged, this,
            &AccountSkinDialog::refreshButtonStates);
    connect(m_accountList, &QListWidget::currentRowChanged, this,
            &AccountSkinDialog::placeRowActions);
    connect(m_capeList, &QListWidget::itemSelectionChanged, this,
            &AccountSkinDialog::refreshButtonStates);
    connect(m_capeList, &QListWidget::itemSelectionChanged, this,
            &AccountSkinDialog::updateCapeCaption);
    connect(m_switchButton, &QPushButton::clicked, this, [this] {
        const QString uuid = selectedUuid();
        if (!uuid.isEmpty()) {
            emit switchRequested(uuid);
        }
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const QString uuid = selectedUuid();
        if (!uuid.isEmpty()) {
            emit removeRequested(uuid);
        }
    });
    connect(m_applyCapeButton, &QPushButton::clicked, this, [this] {
        const QString id = selectedCapeId();
        if (!id.isEmpty()) {
            emit capeSelectRequested(id);
        }
    });
    connect(m_hideCapeButton, &QPushButton::clicked, this,
            &AccountSkinDialog::hideCapeRequested);

    m_accountList->setFixedHeight(kAccountRowHeight + 2);
    updateCapeCaption();
    refreshButtonStates();
}

void AccountSkinDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    OverlayHost *host = nullptr;
    for (auto *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if ((host = qobject_cast<OverlayHost *>(ancestor))) break;
    }
    // 标题行两种形态都留在本页顶上（设计稿：面包屑下面一行大标题）；内嵌页已有返回，不占页脚。
    m_closeButton->setVisible(!(host && host->isOpen()));
    placeRowActions();
}

void AccountSkinDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    const bool narrow = width() < 860;
    m_bodyLayout->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_accountsPanel->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : kAccountsWidth);
}

bool AccountSkinDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_accountList->viewport() && event->type() == QEvent::Resize) {
        placeRowActions();
    }
    return QDialog::eventFilter(watched, event);
}

void AccountSkinDialog::placeRowActions() {
    QListWidgetItem *item = m_accountList->currentItem();
    if (item == nullptr || selectedUuid().isEmpty()) {
        m_rowActions->hide();
        return;
    }
    const QRect row = m_accountList->visualItemRect(item);
    // 「切换到所选」对正在用的那个账号是空操作：那一行只留「删除所选」。
    m_switchButton->setVisible(!item->data(Qt::UserRole + 1).toBool());
    m_rowActions->adjustSize();
    const QSize size = m_rowActions->sizeHint();
    m_rowActions->setGeometry(row.right() - size.width() - 10, row.center().y() - size.height() / 2,
                              size.width(), size.height());
    m_rowActions->show();
    m_rowActions->raise();
    if (m_accountList->property("wtfmlRowActionsWidth").toInt() != size.width() + 10) {
        m_accountList->setProperty("wtfmlRowActionsWidth", size.width() + 10);
        m_accountList->viewport()->update();
    }
}

void AccountSkinDialog::setAccounts(const QList<AccountRow> &rows) {
    m_accountList->clear();
    for (const AccountRow &row : rows) {
        addAccountRow(row);
    }

    if (m_accountList->count() == 0) {
        // 一个账号都没有时摆一句话，而不是一片空白：空白让人以为"还没加载完"。
        QListWidgetItem *placeholder = new QListWidgetItem(noAccountsText(), m_accountList);
        placeholder->setFlags(Qt::NoItemFlags);  // 选不动 —— 它不是一个账号
        placeholder->setForeground(palette().color(QPalette::PlaceholderText));
        placeholder->setSizeHint(QSize(0, kAccountRowHeight));
    } else {
        // 选中"当前"那一条；没有当前账号（凭据被清了）就选第一条。
        int index = 0;
        for (int i = 0; i < m_accountList->count(); ++i) {
            if (m_accountList->item(i)->data(Qt::UserRole + 1).toBool()) {
                index = i;
                break;
            }
        }
        m_accountList->setCurrentRow(index);
    }
    // 高度贴着行数（最多四行，再多就在框里滚）：列表不再有边框，空出一大块会像坏了。
    const int shown = qBound(1, int(m_accountList->count()), 4);
    m_accountList->setFixedHeight(shown * kAccountRowHeight + 2);
    refreshButtonStates();
    placeRowActions();
}

void AccountSkinDialog::addAccountRow(const AccountRow &row) {
    QListWidgetItem *item = new QListWidgetItem(m_accountList);
    // 两个标记可能同时出现（一个离线账号正被用着）：离线那句在前 —— 它说的是"这是个什么
    // 账号"，"当前"是它现在的状态。
    QString text = row.name;
    if (row.offline) {
        text += offlineMark();
    }
    if (row.current) {
        text += currentMark();
    }
    item->setText(text);
    item->setData(Qt::UserRole, row.uuid);
    item->setData(Qt::UserRole + 1, row.current);
    item->setData(kNameRole, row.name);
    item->setData(kOfflineRole, row.offline);
    item->setData(kSavedRole, row.savedAt);
    if (row.offline) {
        // 离线账号没有"登录取自什么时候"这回事 —— 摆一个空的 tooltip 还不如说清它是哪一种。
        item->setToolTip(offlineRowTooltip());
    } else if (!row.savedAt.isEmpty()) {
        const QDateTime savedAt = QDateTime::fromString(row.savedAt, Qt::ISODate);
        if (savedAt.isValid()) {
            item->setToolTip(tr("登录取自 %1")
                                 .arg(savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        }
    }
}

void AccountSkinDialog::clearOfflineNameDraft() { m_offlineNameEdit->clear(); }

void AccountSkinDialog::setStatusText(const QString &text) {
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
}

void AccountSkinDialog::setLooks(const QImage &skin, const QImage &cape, bool slim) {
    m_viewer->setSkin(skin.isNull() ? SkinModel::defaultSteveSkin() : skin, cape, slim);
    m_currentSkin = skin.isNull() ? SkinModel::defaultSteveSkin() : skin;
    m_accountList->viewport()->update();
    // 下拉框跟着**当前皮肤**走：用户多半是"在这个模型上调一下"，而不是每次都从头选。
    const QString variant = slim ? variantSlimValue() : variantClassicValue();
    const int index = m_variantCombo->findData(variant);
    if (index >= 0) {
        m_variantCombo->setCurrentIndex(index);
    }
}

void AccountSkinDialog::setCapes(const QList<CapeRow> &capes) {
    m_capeList->clear();
    for (const CapeRow &cape : capes) {
        QListWidgetItem *item = new QListWidgetItem(m_capeList);
        item->setText(cape.active ? cape.alias + currentMark() : cape.alias);
        item->setToolTip(cape.alias);
        item->setData(Qt::UserRole, cape.id);
        item->setData(Qt::UserRole + 1, cape.active);
        item->setData(Qt::UserRole + 2, cape.url);
    }
    m_capeList->setVisible(!capes.isEmpty());
    updateCapeCaption();
    refreshButtonStates();
}

void AccountSkinDialog::setCapeTexture(const QString &url, const QImage &texture) {
    if (url.isEmpty() || texture.isNull()) return;
    for (int i = 0; i < m_capeList->count(); ++i) {
        QListWidgetItem *item = m_capeList->item(i);
        if (item->data(Qt::UserRole + 2).toString() == url) item->setData(Qt::UserRole + 3, texture);
    }
}

void AccountSkinDialog::updateCapeCaption() {
    // 格子里只有色块，名字写在格子下面（设计稿 .cape-name）：选中的那件，没选就写正披着的那件。
    const QListWidgetItem *shown = m_capeList->currentItem();
    for (int i = 0; shown == nullptr && i < m_capeList->count(); ++i) {
        if (m_capeList->item(i)->data(Qt::UserRole + 1).toBool()) shown = m_capeList->item(i);
    }
    m_capeCaption->setText(shown != nullptr ? shown->text() : QString());
    m_capeCaption->setVisible(shown != nullptr);
}

void AccountSkinDialog::setBusy(bool busy, const QString &what) {
    m_busy = busy;
    m_loginButton->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
    m_uploadButton->setEnabled(!busy);
    // 离线那一档的两颗一起灰（忙的时候下面 refreshButtonStates() 也会按 idle 重算一遍）。
    m_offlineSkinButton->setEnabled(!busy);
    m_offlineSkinResetButton->setEnabled(!busy);
    // 两个列表一起灰：它们的内容正是「正在被刷新」的那两样，忙的时候让用户改选中项，
    // 只会让「我点的是哪个」和「回来的结果是哪个」对不上。
    m_accountList->setEnabled(!busy);
    m_capeList->setEnabled(!busy);
    if (busy) {
        m_statusLabel->setText(what);
    }
    refreshButtonStates();
}

void AccountSkinDialog::setOfflineSkinState(bool offline, bool hasCustomSkin) {
    m_offlineSkin = offline;
    m_hasCustomSkin = hasCustomSkin;
    m_uploadButton->setVisible(!offline);
    m_offlineSkinButton->setVisible(offline);
    m_offlineSkinResetButton->setVisible(offline);
    refreshButtonStates();
}

QString AccountSkinDialog::selectedUuid() const {
    const QListWidgetItem *item = m_accountList->currentItem();
    if (item == nullptr || !item->flags().testFlag(Qt::ItemIsSelectable)) {
        return QString();  // 占位那一行选不动，见 setAccounts()
    }
    return item->data(Qt::UserRole).toString();
}

QString AccountSkinDialog::selectedCapeId() const {
    const QListWidgetItem *item = m_capeList->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

QString AccountSkinDialog::selectedVariant() const {
    const QString value = m_variantCombo->currentData().toString();
    return value.isEmpty() ? variantClassicValue() : value;
}

void AccountSkinDialog::pickSkinFile() {
    // NOTE: 这一处也是独立窗（Windows 自己的文件选择框）—— 理由和取舍见
    // MainWindow::onAddFolderClicked() 上面那一段。
    const QString path = QFileDialog::getOpenFileName(this, tr("选择皮肤图片"), QString(),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    // NOTE: 这里**只**把路径交出去，不自己校验尺寸 —— 尺寸那条规矩在
    // SkinApi::isAcceptableSkinSize() 里（唯一出处），校验失败由 MainWindow 弹窗说。
    emit skinUploadRequested(path, selectedVariant());
}

void AccountSkinDialog::pickOfflineSkinFile() {
    // 和上面那颗同一个选择框；区别只在**交出去的那件事**是什么（见头文件那两个信号）。
    const QString path = QFileDialog::getOpenFileName(this, tr("选择皮肤图片"), QString(),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    // 尺寸那条规矩同样不在这里判（唯一出处 SkinApi::isAcceptableSkinSize()，由 MainWindow 弹窗说）。
    emit offlineSkinPickRequested(path, selectedVariant());
}

void AccountSkinDialog::submitOfflineName() {
    // 名字合不合法**不在这里判**（空、有空白、太长都在 AccountStore::addOffline() 里，
    // 唯一出处）。这里只管把用户填的东西交出去。
    emit offlineAccountRequested(m_offlineNameEdit->text().trimmed());
}

void AccountSkinDialog::refreshButtonStates() {
    // 「切换到所选」「删除所选」「显示所选披风」三件事都**要有一个选中的东西**才做得了。
    // NOTE: 三个都要**同时**看「有没有选中」和「忙不忙」。只看选中那一半的话，
    // setBusy(true) 里末尾那次调用会把它们重新点亮（见 .hpp 里 m_busy 那段）。
    const bool idle = !m_busy;
    const bool haveAccount = !selectedUuid().isEmpty();
    m_switchButton->setEnabled(idle && haveAccount);
    m_removeButton->setEnabled(idle && haveAccount);
    m_applyCapeButton->setEnabled(idle && !selectedCapeId().isEmpty());
    // 加离线账号要跟着"忙不忙"灰：它一旦成功就会把活动槽清空（切到离线身份），而正飞着的
    // 那次登录回来还要往活动槽里写 —— 两个人会打架。
    m_offlineNameEdit->setEnabled(idle);
    m_addOfflineButton->setEnabled(idle);
    m_hideCapeButton->setEnabled(idle);
    // 离线那一档：露着的时候才按 idle 判；「恢复默认外观」还要**确实有东西可撤**。
    m_offlineSkinButton->setEnabled(idle && m_offlineSkin);
    m_offlineSkinResetButton->setEnabled(idle && m_offlineSkin && m_hasCustomSkin);
}
