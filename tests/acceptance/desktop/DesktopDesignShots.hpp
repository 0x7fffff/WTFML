#pragma once
// 设计稿对照截图（--design-shots）：主页三主题、窄窗、宽窗、折叠轨、组件总表。
// 只截图，不登录、不联网、不启动游戏；实例和账号是写在隔离目录里的夹具。
#include "Glass.hpp"
#include "ListRowDelegate.hpp"
#include "MainWindow.hpp"
#include "MainWindowUI.hpp"
#include "OverlayHost.hpp"
#include "TaskProgressBar.hpp"
#include "Theme.hpp"
#include "TitleBar.hpp"
#include "ToolboxPage.hpp"
#include "GameLogWindow.hpp"
#include "ToggleSwitch.hpp"
#include "VersionIcons.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QEnterEvent>
#include <QFile>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QStyle>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QDir>
#include <QVBoxLayout>
#include <functional>

namespace DesignShots {

inline void mute(QWidget *w) {
    QPalette p = w->palette();
    p.setColor(QPalette::WindowText, Theme::tokens().secondary);
    w->setPalette(p);
}

/// 管理页夹具：隔离目录里摆几份"看起来已安装"的实例（<id>/<id>.jar + wtfml.meta），不含真文件。
inline void seedInstances(const QString &versionsDir) {
    const struct { const char *id, *name, *mc, *loader, *loaderVersion; } rows[] = {
        {"spring", "春日生存", "1.21.4", "Fabric", "0.16.9"},
        {"create", "机械动力 Create", "1.20.1", "Forge", "47.3.0"},
        {"sakura", "樱花小屋", "1.21.1", "Fabric", "0.16.9"},
        {"shader", "光影测试", "1.21.1", "OptiFine", "HD U J1"},
        {"skyblock", "空岛生存", "1.21.1", "NeoForge", "21.1.77"},
        {"redstone", "红石实验室", "1.20.1", "", ""},
        {"desert", "沙漠神殿", "1.21.4", "", ""},
    };
    for (const auto &row : rows) {
        const QString dir = versionsDir + QLatin1Char('/') + QString::fromLatin1(row.id);
        QDir().mkpath(dir);
        QFile jar(dir + QLatin1Char('/') + QString::fromLatin1(row.id) + QStringLiteral(".jar"));
        if (jar.open(QIODevice::WriteOnly)) jar.write("stub");
        QJsonObject meta{{"name", QString::fromUtf8(row.name)}, {"realVersion", QString::fromLatin1(row.mc)}};
        if (*row.loader) {
            meta.insert("loaderName", QString::fromLatin1(row.loader));
            meta.insert("loaderVersion", QString::fromLatin1(row.loaderVersion));
        }
        QFile file(dir + QStringLiteral("/wtfml.meta"));
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(meta).toJson());
    }
}

/// 管理页那棵树：悬停第二个实例行（快捷操作只在悬停时出现）。
inline void hoverInstanceRow(MainWindow &window) {
    QTreeWidget *tree = window.ui()->getInstanceList();
    if (tree == nullptr) return;
    int seen = 0;
    for (QTreeWidgetItemIterator it(tree); *it != nullptr; ++it) {
        if ((*it)->data(0, MainWindowUI::instanceIdRole()).toString().isEmpty() || ++seen < 2) continue;
        const QPoint at = tree->visualItemRect(*it).center();
        QHoverEvent hover(QEvent::HoverMove, at, tree->viewport()->mapToGlobal(at), QPoint(-1, -1));
        QApplication::sendEvent(tree->viewport(), &hover);
        tree->viewport()->repaint();
        return;
    }
}

inline QList<MainWindowUI::HomeQuickItem> fixtureItems() {
    const struct { const char *key, *name, *detail; } rows[] = {
        {"spring", "春日生存", "Minecraft 1.21.4 / Fabric 0.16.9\n上次启动：2 小时前"},
        {"create", "机械动力 Create", "Minecraft 1.20.1 / Forge 47.3.0\n上次启动：昨天"},
        {"sakura", "樱花小屋", "Minecraft 1.21.1 / Fabric 0.16.5\n上次启动：3 天前"},
        {"shader", "光影测试", "Minecraft 1.21.1 / Iris 1.8.1\n上次启动：上周"},
        {"skyblock", "空岛生存", "Minecraft 1.21.1 / NeoForge 21.1.77\n上次启动：上周"},
    };
    QList<MainWindowUI::HomeQuickItem> items;
    for (const auto &row : rows) {
        MainWindowUI::HomeQuickItem item;
        item.key = QString::fromUtf8(row.key);
        item.name = QString::fromUtf8(row.name);
        item.detail = QString::fromUtf8(row.detail);
        item.recent = true;
        item.modCount = item.key == QLatin1String("spring") ? 42 : 12;
        items.append(item);
    }
    items.first().current = true;
    items[1].favorite = items[2].favorite = true;
    return items;
}

inline void seedHome(MainWindow &window) {
    const auto items = fixtureItems();
    QList<MainWindowUI::HomeQuickItem> favorites;
    for (const auto &item : items)
        if (item.favorite) favorites.append(item);
    window.ui()->setHomeQuickRows({items.first()}, favorites, items);
}

/// 组件总表：左边折叠后的导航轨，右边 14 种控件 × 6 态。
class Gallery : public QMainWindow {
public:
    QList<QWidget *> focusCells;
    QListWidget *hoverList = nullptr;

    Gallery() {
        setWindowTitle(QStringLiteral("WTFML 组件总表"));
        auto *backdrop = new Glass::Backdrop(this);
        setCentralWidget(backdrop);
        auto *shell = new QHBoxLayout(backdrop);
        shell->setContentsMargins(0, 0, 0, 0);
        shell->setSpacing(0);
        auto *rail = new TitleBar(backdrop);
        rail->setNavItems({QStringLiteral("主页"), QStringLiteral("管理"), QStringLiteral("下载"),
                           QStringLiteral("设置"), QStringLiteral("智能体")});
        rail->setCurrentNav(3);
        rail->setCollapsed(true);
        auto *footer = new QWidget(rail);
        auto *footerLayout = new QVBoxLayout(footer);
        footerLayout->setContentsMargins(0, 0, 0, 0);
        auto *ring = new TaskProgressBar(footer);
        ring->setRing(true);
        ring->setFixedSize(28, 28);
        ring->setRange(0, 0);
        footerLayout->addWidget(ring, 0, Qt::AlignHCenter);
        rail->setFooter(footer);
        shell->addWidget(rail);
        auto *right = new QVBoxLayout;
        right->setContentsMargins(0, 0, 0, 0);
        right->setSpacing(0);
        rail->captionStrip()->setParent(backdrop);
        right->addWidget(rail->captionStrip());
        auto *page = new QWidget(backdrop);
        right->addWidget(page, 1);
        shell->addLayout(right, 1);
        build(page);
    }

private:
    QWidget *cell(QWidget *w, int state) {
        // state：0 常态 1 悬停 2 按下 3 选中 4 禁用 5 焦点
        auto *button = qobject_cast<QAbstractButton *>(w);
        if (state == 1) w->setAttribute(Qt::WA_UnderMouse, true);
        if (state == 1 && qobject_cast<ToggleSwitch *>(w)) {
            QEnterEvent enter(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
            QApplication::sendEvent(w, &enter);
        }
        if (state == 1 && qobject_cast<TitleBarButton *>(w)) {
            QEvent enter(QEvent::HoverEnter);
            QApplication::sendEvent(w, &enter);
        }
        if (state == 2 && button) button->setDown(true);
        if (state == 2) if (auto *slider = qobject_cast<QSlider *>(w)) slider->setSliderDown(true);
        if (state == 3 && button) { button->setCheckable(true); button->setChecked(true); }
        if (state == 4) w->setEnabled(false);
        if (state == 5) focusCells.append(w);
        return w;
    }

    static QLabel *head(const QString &text, QWidget *parent, int px = 13) {
        auto *label = new QLabel(text, parent);
        QFont f = Theme::handFont(label->font(), Theme::HandFont::Kai);
        f.setPixelSize(px);
        f.setLetterSpacing(QFont::AbsoluteSpacing, px >= 20 ? 1.5 : 2);
        label->setFont(f);
        QPalette p = label->palette();
        p.setColor(QPalette::WindowText, px >= 20 ? Theme::tokens().text : Theme::tokens().secondary);
        label->setPalette(p);
        return label;
    }

    void build(QWidget *page) {
        auto *outer = new QVBoxLayout(page);
        outer->setContentsMargins(36, 0, 40, 24);
        outer->setSpacing(14);
        auto *titleRow = new QHBoxLayout;
        auto *title = head(QStringLiteral("组件总表"), page, 30);
        QFont tf = Theme::handFont(title->font(), Theme::HandFont::KaiLight);
        tf.setPixelSize(30);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        title->setFont(tf);
        titleRow->addWidget(title);
        titleRow->addStretch(1);
        auto *note = new QLabel(QStringLiteral("左侧为折叠后的导航轨（只剩图标） · Qt 实拍，焦点列逐格取焦后拼合"), page);
        mute(note);
        titleRow->addWidget(note);
        outer->addLayout(titleRow);

        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(14);
        const QStringList states{QStringLiteral("常态"), QStringLiteral("悬停"), QStringLiteral("按下"),
                                 QStringLiteral("选中 / 开"), QStringLiteral("禁用"), QStringLiteral("焦点（键盘）")};
        for (int c = 0; c < states.size(); ++c) grid->addWidget(head(states.at(c), page), 0, c + 1);
        grid->setColumnMinimumWidth(0, 96);
        for (int c = 1; c <= 6; ++c) grid->setColumnStretch(c, 1);
        int row = 1;
        const auto addRow = [&](const QString &name, const std::function<QWidget *(int)> &make, QList<int> skip = {}) {
            grid->addWidget(head(name, page, 14), row, 0);
            for (int s = 0; s < 6; ++s) {
                if (skip.contains(s)) {
                    auto *dash = new QLabel(QStringLiteral("—"), page);
                    mute(dash);
                    grid->addWidget(dash, row, s + 1, Qt::AlignLeft | Qt::AlignVCenter);
                    continue;
                }
                grid->addWidget(cell(make(s), s), row, s + 1, Qt::AlignLeft | Qt::AlignVCenter);
            }
            ++row;
        };
        const auto pushButton = [page](const QString &text, Theme::ButtonKind kind, const QString &icon = {}) {
            auto *b = new QPushButton(text, page);
            Theme::markButton(b, kind);
            b->setFocusPolicy(Qt::StrongFocus);
            if (!icon.isEmpty()) {
                const QColor ink = kind == Theme::ButtonKind::Primary ? Theme::tokens().onPrimary
                                   : kind == Theme::ButtonKind::Danger ? Theme::tokens().danger
                                   : Theme::tokens().onDanger;
                b->setIcon(QIcon(VersionIcons::line(icon, 14, ink, 2.0)));
                b->setIconSize(QSize(14, 14));
            }
            return b;
        };
        addRow(QStringLiteral("主按钮"), [&](int) { return pushButton(QStringLiteral("启动"), Theme::ButtonKind::Primary, QStringLiteral("play")); }, {3});
        addRow(QStringLiteral("次按钮"), [&](int) { return pushButton(QStringLiteral("导入"), Theme::ButtonKind::Neutral); });
        addRow(QStringLiteral("文字按钮"), [&](int) { return pushButton(QStringLiteral("全部实例  ›"), Theme::ButtonKind::Ghost); }, {3});
        addRow(QStringLiteral("危险按钮"), [&](int) { return pushButton(QStringLiteral("删除"), Theme::ButtonKind::Danger, QStringLiteral("trash")); }, {3});
        addRow(QStringLiteral("危险实心"), [&](int) { return pushButton(QStringLiteral("删除实例"), Theme::ButtonKind::DangerSolid); }, {3});
        addRow(QStringLiteral("图标按钮"), [&](int s) {
            auto *b = new QPushButton(page);
            Theme::markIconButton(b);
            b->setFixedSize(36, 36);
            b->setFocusPolicy(Qt::StrongFocus);
            const QColor ink = s == 3 ? Theme::tokens().deep : s == 4 ? Theme::tokens().disabled
                               : s == 1 || s == 2 ? Theme::tokens().text : Theme::tokens().secondary;
            b->setIcon(QIcon(VersionIcons::line(QStringLiteral("star"), 18, ink)));
            b->setIconSize(QSize(18, 18));
            return b;
        });
        addRow(QStringLiteral("输入框"), [&](int s) {
            auto *e = new QLineEdit(page);
            e->setFixedSize(150, 36);
            e->setPlaceholderText(QStringLiteral("搜索实例"));
            if (s == 3) { e->setText(QStringLiteral("-Xmx99G")); e->setProperty("wtfmlError", true); }
            if (s == 4) e->setPlaceholderText(QStringLiteral("由实例接管"));
            if (s == 5) e->setText(QStringLiteral("春日生存"));
            return e;
        }, {2});
        addRow(QStringLiteral("下拉"), [&](int) {
            auto *c = new QComboBox(page);
            c->addItem(QStringLiteral("跟随系统"));
            c->setFixedSize(150, 36);
            return c;
        }, {2, 3});
        // 设计稿禁用列画的是「开着但禁用」，焦点列开关也是开着的。
        addRow(QStringLiteral("开关"), [&](int s) {
            auto *t = new ToggleSwitch(page);
            t->setChecked(s >= 4);
            return t;
        });
        addRow(QStringLiteral("复选"), [&](int s) {
            auto *c = new QCheckBox(page);
            c->setFocusPolicy(Qt::StrongFocus);
            c->setChecked(s == 4);
            return c;
        });
        addRow(QStringLiteral("滑块"), [&](int) {
            auto *s = new QSlider(Qt::Horizontal, page);
            s->setRange(0, 100);
            s->setValue(40);
            s->setFixedWidth(150);
            return s;
        }, {3});
        addRow(QStringLiteral("胶囊筛选"), [&](int) {
            auto *b = new QPushButton(QStringLiteral("模组  42"), page);
            Theme::markChipButton(b);
            b->setFocusPolicy(Qt::StrongFocus);
            return b;
        });
        addRow(QStringLiteral("导航项"), [&](int s) {
            auto *b = new TitleBarButton(TitleBarButton::Glyph::Text, Theme::tokens().hover, Theme::tokens().text, page);
            b->setNavIcon(TitleBarButton::NavIcon::Download);
            b->setLabel(QStringLiteral("下载"));
            QFont f = Theme::handFont(b->font(), Theme::HandFont::Kai);
            f.setPixelSize(16);
            b->setFont(f);
            b->setFixedSize(150, 42);
            b->setPressColor(Theme::tokens().fillPress);
            b->setInkColor(s == 3 ? Theme::tokens().deep : Theme::tokens().secondary);
            if (s == 3) { b->setBaseColor(Theme::tokens().selected); b->setCurrent(true); }
            return b;
        });
        outer->addLayout(grid);

        // 下半：列表行 + 徽标 / 分段 / 进度。
        auto *lower = new QHBoxLayout;
        lower->setSpacing(36);
        auto *listCol = new QVBoxLayout;
        listCol->addWidget(head(QStringLiteral("列表行"), page, 20));
        auto *list = new QListWidget(page);
        list->setFrameShape(QFrame::NoFrame);
        ListRowDelegate::install(list);
        list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"));  // 页面里的列表是透明底
        // 设计稿 BLOCKS 下标：0 草 / 7 铜 / 5 樱花。
        const struct { int block; const char *title, *sub, *meta; } rows[] = {
            {0, "春日生存", "Fabric 0.16.9 · 1.21.4", "2 小时前"},
            {7, "机械动力 Create", "Forge 47.3 · 1.20.1", ""},
            {5, "樱花小屋", "Fabric 0.16.9 · 1.21.1", "昨天"},
        };
        for (const auto &row : rows) {
            auto *item = new QListWidgetItem(QString::fromUtf8(row.title), list);
            item->setData(Qt::DecorationRole, VersionIcons::isoBlock(row.block, 48));
            item->setData(ListRowDelegate::SubtitleRole, QString::fromUtf8(row.sub));
            item->setData(ListRowDelegate::MetaRole, QString::fromUtf8(row.meta));
        }
        list->setCurrentRow(2);
        list->setFixedHeight(3 * ListRowDelegate::rowHeight() + 4);
        hoverList = list;
        listCol->addWidget(list);
        lower->addLayout(listCol, 1);

        auto *misc = new QGridLayout;
        misc->setHorizontalSpacing(18);
        misc->setVerticalSpacing(14);
        misc->addWidget(head(QStringLiteral("徽标 · 分段 · 进度"), page, 20), 0, 0, 1, 2);
        misc->addWidget(head(QStringLiteral("徽标"), page), 1, 0);
        auto *badges = new QHBoxLayout;
        badges->setSpacing(8);
        const QList<QPair<QString, Theme::BadgeTone>> tones{{QStringLiteral("原版"), Theme::BadgeTone::Neutral},
            {QStringLiteral("Fabric"), Theme::BadgeTone::Accent}, {QStringLiteral("● 运行中"), Theme::BadgeTone::Ok},
            {QStringLiteral("可更新"), Theme::BadgeTone::Warn}, {QStringLiteral("文件损坏"), Theme::BadgeTone::Danger}};
        for (const auto &[text, tone] : tones) {
            auto *badge = new QLabel(text, page);
            Theme::markBadge(badge, tone);
            badges->addWidget(badge);
        }
        badges->addStretch(1);
        misc->addLayout(badges, 1, 1);
        misc->addWidget(head(QStringLiteral("分段"), page), 2, 0);
        auto *seg = new QWidget(page);
        auto *segLayout = new QHBoxLayout(seg);
        segLayout->setContentsMargins(3, 3, 3, 3);
        segLayout->setSpacing(2);
        QList<QPushButton *> segments;
        for (const QString &text : {QStringLiteral("常态"), QStringLiteral("悬停"), QStringLiteral("按下"),
                                    QStringLiteral("选中"), QStringLiteral("禁用")}) {
            auto *b = new QPushButton(text, seg);
            segments.append(b);
            segLayout->addWidget(b);
        }
        Theme::markSegmented(seg, segments);
        segments[1]->setAttribute(Qt::WA_UnderMouse, true);
        segments[2]->setDown(true);
        segments[3]->setChecked(true);
        segments[4]->setEnabled(false);
        auto *segRow = new QHBoxLayout;
        segRow->addWidget(seg);
        segRow->addStretch(1);
        misc->addLayout(segRow, 2, 1);
        misc->addWidget(head(QStringLiteral("进度"), page), 3, 0);
        auto *bars = new QVBoxLayout;
        bars->setSpacing(10);
        for (const auto &[text, value] : {QPair<QString, int>{QStringLiteral("下载 Fabric 1.21.4 · 128 / 342 个文件    37%"), 37},
                                          QPair<QString, int>{QStringLiteral("正在校验…    不定"), -1},
                                          QPair<QString, int>{QStringLiteral("安装完成    100%"), 100}}) {
            auto *label = new QLabel(text, page);
            bars->addWidget(label);
            auto *bar = new TaskProgressBar(page);
            bar->setFixedHeight(4);
            if (value < 0) bar->setRange(0, 0); else { bar->setRange(0, 100); bar->setValue(value); }
            bars->addWidget(bar);
        }
        misc->addLayout(bars, 3, 1);
        misc->setColumnStretch(1, 1);
        lower->addLayout(misc, 1);
        outer->addLayout(lower);
        outer->addStretch(1);
    }
};

/// 焦点那一列：逐格取真焦点（键盘那一档）后从整窗截那一小块，拼回总图。
inline QImage grabGallery(Gallery &gallery) {
    if (QListWidget *list = gallery.hoverList) {  // 第二行走一遍真的悬停（快捷操作只在悬停时出现）
        const QPoint at = list->visualItemRect(list->item(1)).center();
        QHoverEvent hover(QEvent::HoverMove, at, list->viewport()->mapToGlobal(at), QPoint(-1, -1));
        QApplication::sendEvent(list->viewport(), &hover);
        list->viewport()->repaint();
    }
    QImage image = gallery.grab().toImage();
    QPainter painter(&image);
    const qreal dpr = image.devicePixelRatio();
    for (QWidget *w : std::as_const(gallery.focusCells)) {
        w->setFocus(Qt::TabFocusReason);
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(w, &key);  // 「键盘在用」那一档只认真按键
        w->setFocus(Qt::TabFocusReason);
        w->setProperty("wtfmlKeyboardFocus", true);
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
        QApplication::processEvents();
        const QRect area = QRect(w->mapTo(&gallery, QPoint(0, 0)), w->size()).adjusted(-4, -4, 4, 4);
        const QImage piece = gallery.grab(area).toImage();
        painter.drawImage(QRectF(QPointF(area.topLeft()), QSizeF(area.size())), piece);
        Q_UNUSED(dpr);
    }
    return image;
}

/// 依次截：三主题主页 1280×800、暗色 900×620 / 1600×1000、折叠轨、组件总表。
inline void run(QApplication &app, MainWindow &window, const QString &root) {
    struct Shot { QString file; QString theme; QSize size; bool collapsed; int tab = MainWindowUI::homeTabIndex(); QString page = {}; };
    const int manage = MainWindowUI::instancesTabIndex();
    const int download = MainWindowUI::downloadTabIndex();
    const int agent = MainWindowUI::agentTabIndex();
    const int home = MainWindowUI::homeTabIndex();
    static const QList<Shot> shots = [&]() { QList<Shot> list{
        {QStringLiteral("home-dark.png"), QStringLiteral("dark"), {1280, 800}, false},
        {QStringLiteral("home-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false},
        {QStringLiteral("home-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false},
        {QStringLiteral("home-dark-900x620.png"), QStringLiteral("dark"), {900, 620}, false},
        {QStringLiteral("home-dark-1600x1000.png"), QStringLiteral("dark"), {1600, 1000}, false},
        {QStringLiteral("rail-collapsed-dark.png"), QStringLiteral("dark"), {1280, 800}, true},
        {QStringLiteral("manage-dark.png"), QStringLiteral("dark"), {1280, 800}, false, manage},
        {QStringLiteral("manage-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, manage},
        {QStringLiteral("manage-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, manage},
        {QStringLiteral("download-dark.png"), QStringLiteral("dark"), {1280, 800}, false, download},
        {QStringLiteral("download-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, download},
        {QStringLiteral("download-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, download},
        // 实例二级页（设计稿 instance-*）：「机械动力 Create」那一份夹具，按页开。
        {QStringLiteral("instance-overview-dark.png"), QStringLiteral("dark"), {1280, 800}, false, manage, QStringLiteral("overview")},
        {QStringLiteral("instance-overview-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, manage, QStringLiteral("overview")},
        {QStringLiteral("instance-overview-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, manage, QStringLiteral("overview")},
        {QStringLiteral("instance-launch-dark.png"), QStringLiteral("dark"), {1280, 800}, false, manage, QStringLiteral("launch")},
        {QStringLiteral("instance-mods-dark.png"), QStringLiteral("dark"), {1280, 800}, false, manage, QStringLiteral("mod")},
        // 智能体页、工具箱（设计稿 agent / panels-toolbox）：页键 toolbox = 底栏面板，toolbox:<键> = 展开那一页。
        {QStringLiteral("agent-dark.png"), QStringLiteral("dark"), {1280, 800}, false, agent},
        {QStringLiteral("agent-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, agent},
        {QStringLiteral("agent-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, agent},
        {QStringLiteral("toolbox-dark.png"), QStringLiteral("dark"), {1280, 800}, false, home, QStringLiteral("toolbox")},
        {QStringLiteral("toolbox-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, home, QStringLiteral("toolbox")},
        {QStringLiteral("toolbox-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, home, QStringLiteral("toolbox")},
        // 游戏日志窗（独立顶层窗，自绘标题条）：页键 gamelog = 另开一只日志窗、抓它自己。
        {QStringLiteral("gamelog-dark.png"), QStringLiteral("dark"), {1280, 800}, false, home, QStringLiteral("gamelog")},
        {QStringLiteral("gamelog-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, home, QStringLiteral("gamelog")},
        {QStringLiteral("gamelog-ocean.png"), QStringLiteral("ocean"), {1280, 800}, false, home, QStringLiteral("gamelog")},
        {QStringLiteral("toolbox-page-dark.png"), QStringLiteral("dark"), {1280, 800}, false, home, QStringLiteral("toolbox:anvil")},
        {QStringLiteral("toolbox-page-emerald.png"), QStringLiteral("emerald"), {1280, 800}, false, home, QStringLiteral("toolbox:anvil")},
    };
        // 精修轮逐页自检：启动设置三主题、工具箱其余九页三主题。
        for (const QString &theme : {QStringLiteral("emerald"), QStringLiteral("ocean")})
            list.append({QStringLiteral("instance-launch-%1.png").arg(theme), theme, {1280, 800}, false, manage, QStringLiteral("launch")});
        for (const char *key : {"text", "coord", "server", "hash", "beacon", "brewing", "banner", "trim"})
            for (const QString &theme : {QStringLiteral("dark"), QStringLiteral("emerald"), QStringLiteral("ocean")})
                list.append({QStringLiteral("tool-%1-%2.png").arg(QLatin1String(key), theme), theme, {1280, 800}, false, home,
                             QStringLiteral("toolbox:") + QLatin1String(key)});
        // 只出名字含这段字的几张（逐页精修时不必每次跑满 56 张）。
        const QString only = qEnvironmentVariable("WTFML_DESIGN_SHOT_FILTER");
        if (!only.isEmpty())
            list.removeIf([&only](const Shot &shot) { return !shot.file.contains(only); });
        return list;
    }();
    static int index = 0;
    static QJsonArray report;
    static std::function<void()> step;
    step = [&app, &window, root]() {
        if (index >= shots.size()) {
            Theme::setSpec(Theme::presetSpec(QStringLiteral("dark")));
            Theme::apply(app);
            auto *gallery = new Gallery;
            gallery->resize(1280, 1100);
            gallery->show();
            gallery->activateWindow();
            QTimer::singleShot(300, gallery, [gallery]() {
                gallery->resize(1280, 1100);
                // 全局 QSS polish 会把 setFixedHeight 钉的最小高度改回去（输入框 23、胶囊 25），这里按钉的那一档再钉一次。
                for (QWidget *w : gallery->findChildren<QWidget *>())
                    if (w->maximumHeight() < QWIDGETSIZE_MAX && w->minimumHeight() < w->maximumHeight())
                        w->setMinimumHeight(w->maximumHeight());
            });
            QTimer::singleShot(900, gallery, [&app, gallery, root]() {
                const bool saved = grabGallery(*gallery).save(root + QStringLiteral("/components-dark-1280x1100.png"));
                QJsonArray cells;
                for (QWidget *w : std::as_const(gallery->focusCells))
                    cells.append(QStringLiteral("%1 %2x%3 min%4 max%5").arg(QString::fromLatin1(w->metaObject()->className()))
                                     .arg(w->width()).arg(w->height()).arg(w->minimumHeight()).arg(w->maximumHeight()));
                report.append(QJsonObject{{"cells", cells}});
                report.append(QJsonObject{{"file", "components-dark-1280x1100.png"}, {"saved", saved},
                                          {"width", gallery->width()}, {"height", gallery->height()}});
                QFile out(root + QStringLiteral("/design-shots.json"));
                const bool ok = out.open(QIODevice::WriteOnly) &&
                                out.write(QJsonDocument(QJsonObject{{"devicePixelRatio", gallery->devicePixelRatioF()},
                                                                    {"shots", report}}).toJson()) > 0;
                app.exit(ok && saved ? 0 : 1);
            });
            return;
        }
        const Shot shot = shots.at(index++);
        Theme::setSpec(Theme::presetSpec(shot.theme));
        Theme::apply(app);
        window.showNormal();
        window.resize(shot.size);
        window.ui()->getTabWidget()->setCurrentIndex(shot.tab);
        if (auto *bar = window.findChild<TitleBar *>()) bar->setCollapsed(shot.collapsed);
        seedHome(window);
        // 竖屏 / 小屏上 Windows 首次会把超出屏宽的窗口夹回屏宽，排完版再要一次尺寸。
        QTimer::singleShot(300, &window, [&window, shot]() { window.resize(shot.size); });
        // 启动后头一张时主窗口自己还会刷一次主页（在线版本列表回来后按隔离目录里的空实例重排），
        // 抓图前再灌一次夹具，再让布局走一拍。
        QTimer::singleShot(700, &window, [&window]() { seedHome(window); });
        QTimer::singleShot(850, &window, [&window, shot]() {
            if (shot.page == QStringLiteral("gamelog")) {
                auto *log = new GameLogWindow(QStringLiteral("春日生存"), nullptr);
                log->setObjectName(QStringLiteral("designShotGameLog"));
                log->appendText(QStringLiteral(
                    "[12:00:01] [main/INFO]: Loading Minecraft 1.21.4 with Fabric Loader 0.16.9\n"
                    "[12:00:02] [main/INFO]: Loading 42 mods\n"
                    "[12:00:03] [main/WARN]: Mod sodium uses a deprecated mixin config\n"
                    "[12:00:05] [Render thread/INFO]: Backend library: LWJGL version 3.3.3\n"
                    "[12:00:07] [Render thread/ERROR]: Failed to load texture minecraft:textures/misc/unknown.png\n"
                    "[12:00:09] [Server thread/INFO]: Preparing spawn area: 84%\n"));
                log->resize(960, 600);
                log->show();
            } else if (shot.page.startsWith(QStringLiteral("toolbox"))) {
                if (auto *open = window.findChild<QAbstractButton *>(MainWindowUI::toolboxButtonName())) open->click();
                const QString key = shot.page.section(QLatin1Char(':'), 1);
                bool ok = false;
                const ToolboxPage::Tool tool = ToolboxPage::toolFromKey(key, &ok);
                if (ok) {
                    if (auto *button = window.findChild<QPushButton *>(ToolboxPage::toolButtonName(tool))) button->click();
                }
            } else if (!shot.page.isEmpty()) QMetaObject::invokeMethod(&window, "openInstanceSettings", Q_ARG(QString, QStringLiteral("create")), Q_ARG(QString, shot.page));
            else if (shot.tab == MainWindowUI::instancesTabIndex()) hoverInstanceRow(window);
        });
        QTimer::singleShot(shot.page.isEmpty() ? 900 : 1600, &window, [&window, root, shot]() {
            GameLogWindow *log = nullptr;
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top->objectName() == QStringLiteral("designShotGameLog")) log = qobject_cast<GameLogWindow *>(top);
            QPixmap shotImage = log != nullptr ? log->grab() : window.grab();
            if (log != nullptr) log->close();
            // 工具箱面板是 Qt::Popup（另一个顶层窗），window.grab() 抓不到它：按它在窗口里的位置贴上去。
            if (auto *panel = window.findChild<QFrame *>(MainWindowUI::toolboxPanelName()); panel != nullptr && panel->isVisible()) {
                QPainter painter(&shotImage);
                painter.drawPixmap(window.mapFromGlobal(panel->mapToGlobal(QPoint(0, 0))), panel->grab());
            }
            const bool saved = shotImage.save(root + QLatin1Char('/') + shot.file);
            report.append(QJsonObject{{"file", shot.file}, {"saved", saved}, {"width", window.width()},
                                      {"height", window.height()}, {"dpr", window.devicePixelRatioF()},
                                      {"maxWidth", window.maximumWidth()}, {"screenWidth", window.screen()->availableGeometry().width()},
                                      {"state", int(window.windowState())}});
            if (!shot.page.isEmpty()) window.ui()->overlay()->closeOverlay();
            if (auto *panel = window.findChild<QFrame *>(MainWindowUI::toolboxPanelName())) panel->close();
            QTimer::singleShot(shot.page.isEmpty() ? 0 : 300, &window, step);
        });
    };
    QTimer::singleShot(1200, &window, step);
}

}  // namespace DesignShots
