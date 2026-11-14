#include "Support.hpp"

#include "MainWindowPageLayout.hpp"
#include "MainWindowUI.hpp"
#include "Theme.hpp"

#include <QFontMetrics>
#include <QEnterEvent>

// 长实例名的字号（步骤 127）：主页「当前实例」名在 40–28 里挑、实例设置页眉在 26–20 里挑，
// 都是放得下整名的最大一号，到下限还放不下才收省略号、悬停给全名；行高按上限钉死，
// 名字长短不挪下面那一行和按钮。挑字号的公共函数是 Theme::fitPixelSize（每日一言也走它）。
namespace {

const QString kShort = QStringLiteral("生存");
const QString kMid = QStringLiteral("我的第一个生存世界整合包");                             // 12 字
const QString kLongHan = QStringLiteral("群峦叠翠云海翻腾的远古遗迹探险与机械动力工业整合包第三季重制");  // 30 字
const QString kLongLatin = QStringLiteral("All The Mods 9 - To the Sky Expert Edition with Create Addons");

void settle(QApplication &app) {
    for (int i = 0; i < 12; ++i) app.processEvents();
}

int advanceAt(const QLabel *label, int px) {
    QFont f = label->font();
    f.setPixelSize(px);
    return QFontMetrics(f).horizontalAdvance(label->text());
}

QString detail(const QLabel *label) {
    if (label == nullptr) return QStringLiteral("没找到");
    return QStringLiteral("%1px，名宽 %2 / 可用 %3，提示「%4」")
        .arg(label->font().pixelSize())
        .arg(advanceAt(label, label->font().pixelSize()))
        .arg(label->contentsRect().width())
        .arg(label->toolTip());
}

/// 放得下整名、且再大一号就放不下（或已到上限）。
bool largestFit(const QLabel *label, int lo, int hi) {
    const int px = label->font().pixelSize();
    const int room = label->contentsRect().width();
    return px >= lo && px <= hi && advanceAt(label, px) <= room && (px == hi || advanceAt(label, px + 1) > room) &&
           label->toolTip().isEmpty();
}

bool truncatedAtFloor(const QLabel *label, int lo) {
    return label->font().pixelSize() == lo && advanceAt(label, lo) > label->contentsRect().width() &&
           label->toolTip() == label->text();
}

int yIn(QWidget *w, QWidget *root) { return w == nullptr ? -1 : w->mapTo(root, QPoint(0, 0)).y(); }

}  // namespace

int UiLongInstanceNameChecks(QApplication &app) {
    Theme::setSpec(Theme::defaultSpec());
    Theme::apply(app);

    // ---------- 主页「当前实例」 ----------
    {
        constexpr int lo = MainWindowPageLayout::kHomeCurrentNameMinPixelSize;
        constexpr int hi = MainWindowPageLayout::kHomeCurrentNameMaxPixelSize;
        check(lo >= 26, "LN1 主页实例名字号下限不低于页标题的 26", QString::number(lo));
        QMainWindow window;
        MainWindowUI ui(&window);
        window.resize(1280, 800);
        window.show();
        struct Shown {
            QLabel *name = nullptr;
            int metaY = -1, launchY = -1, editY = -1, nameH = -1;
        };
        const auto showName = [&](const QString &name) {
            MainWindowUI::HomeQuickItem item;
            item.key = QStringLiteral("demo");
            item.name = name;
            item.detail = QStringLiteral("1.21.4 / Fabric 0.16.9");
            item.current = true;
            ui.setHomeQuickRows({item}, {item}, {item});
            ui.setHomeQuickMode(MainWindowUI::HomeQuickMode::Current);
            settle(app);
            Shown s;
            s.name = window.findChild<QLabel *>(QStringLiteral("homeCurrentName"));
            s.metaY = yIn(window.findChild<QWidget *>(QStringLiteral("homeCurrentVersion")), &window);
            s.launchY = yIn(window.findChild<QWidget *>(QStringLiteral("homeCurrentLaunch")), &window);
            s.editY = yIn(window.findChild<QWidget *>(QStringLiteral("homeCurrentEdit")), &window);
            s.nameH = s.name != nullptr ? s.name->height() : -1;
            return s;
        };

        const Shown shortName = showName(kShort);
        check(shortName.name != nullptr && shortName.name->font().pixelSize() == hi && shortName.name->toolTip().isEmpty(),
              "LN2 短名「生存」用上限 40，不带悬停提示", detail(shortName.name));
        const Shown mid = showName(kMid);
        check(mid.name != nullptr && largestFit(mid.name, lo, hi), "LN3 12 字的名字：字号在 28–40 里，是整名放得下的最大一号",
              detail(mid.name));
        const int midWide = mid.name != nullptr ? mid.name->font().pixelSize() : -1;
        // 拉窄、拉宽都重挑：窄窗不比宽窗大，回到宽窗字号回得去。
        window.resize(900, 620);
        settle(app);
        auto *narrowName = window.findChild<QLabel *>(QStringLiteral("homeCurrentName"));
        check(narrowName != nullptr && narrowName->font().pixelSize() <= midWide &&
                  (largestFit(narrowName, lo, hi) || truncatedAtFloor(narrowName, lo)),
              "LN4 窗口拉窄到 900：重挑字号，不比宽窗大", detail(narrowName));
        window.resize(1280, 800);
        settle(app);
        check(narrowName != nullptr && narrowName->font().pixelSize() == midWide,
              "LN5 拉回 1280：字号回到宽窗那一号", detail(narrowName));

        const Shown longHan = showName(kLongHan);
        check(longHan.name != nullptr && truncatedAtFloor(longHan.name, lo), "LN6 30 字的名字：到下限 28 还放不下，收省略号、提示全名",
              detail(longHan.name));
        if (auto *picker = window.findChild<QWidget *>(MainWindowUI::homeCurrentPickerName())) {
            QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(picker->mapToGlobal(QPoint(4, 4))));
            QApplication::sendEvent(picker, &enter);
            check(picker->toolTip() == kLongHan, "LN7 名字那格对鼠标透明：全名提示由整块可点的面代出", picker->toolTip());
        } else {
            check(false, "LN7 找到「当前实例」那一块可点的面");
        }
        const Shown longLatin = showName(kLongLatin);
        check(longLatin.name != nullptr && truncatedAtFloor(longLatin.name, lo), "LN8 60 字符英文名：下限 28 且截断",
              detail(longLatin.name));
        const auto ys = [](const Shown &s) {
            return QStringLiteral("名高 %1 元信息 y=%2 启动 y=%3 设置 y=%4").arg(s.nameH).arg(s.metaY).arg(s.launchY).arg(s.editY);
        };
        check(shortName.metaY > 0 && shortName.nameH == longHan.nameH && shortName.metaY == longHan.metaY &&
                  shortName.launchY == longHan.launchY && shortName.editY == longHan.editY &&
                  shortName.metaY == longLatin.metaY && shortName.launchY == longLatin.launchY,
              "LN9 短名和长名：名字行高、元信息行和两颗按钮的 y 一样",
              QStringLiteral("短：%1；长：%2；英：%3").arg(ys(shortName), ys(longHan), ys(longLatin)));
    }

    // ---------- 实例设置页眉 ----------
    {
        constexpr int hi = Theme::textSize(Theme::TextRole::Display);
        constexpr int lo = 20;
        InstanceSettingsDialog::GlobalValues global;
        const auto open = [&](InstanceSettingsDialog &dialog) {
            dialog.setHeaderMeta(QStringLiteral("1.21.4 · Fabric 0.16.9"));
            dialog.resize(820, 560);
            dialog.show();
            settle(app);
            return dialog.findChild<QLabel *>(QStringLiteral("instanceHeaderTitle"));
        };
        const auto headerHeight = [](QWidget &dialog) {
            auto *header = dialog.findChild<QWidget *>(QStringLiteral("instanceSettingsHeader"));
            return header != nullptr ? header->height() : -1;
        };
        InstanceSettingsDialog shortDlg(kShort, kShort, InstanceSettingsDialog::Override(), global, QStringList(), false);
        QLabel *shortTitle = open(shortDlg);
        check(shortTitle != nullptr && shortTitle->font().pixelSize() == hi && shortTitle->toolTip().isEmpty(),
              "LN10 页眉短名用 Display 26", detail(shortTitle));
        const int shortHeader = headerHeight(shortDlg);
        const int shortTitleH = shortTitle != nullptr ? shortTitle->height() : -1;

        InstanceSettingsDialog midDlg(kLongHan, kLongHan, InstanceSettingsDialog::Override(), global, QStringList(), false);
        QLabel *midTitle = open(midDlg);
        check(midTitle != nullptr && (largestFit(midTitle, lo, hi) || truncatedAtFloor(midTitle, lo)) &&
                  midTitle->font().pixelSize() < hi,
              "LN11 页眉 30 字名：收到 20–26 里放得下的最大一号（还放不下才截断）", detail(midTitle));

        // 改名那条路（setInstanceDisplayName）也重挑字号。
        shortDlg.setInstanceDisplayName(kLongLatin);
        settle(app);
        check(shortTitle != nullptr && shortTitle->text() == kLongLatin && truncatedAtFloor(shortTitle, lo),
              "LN12 改成 60 字符英文名：下限 20、截断、悬停提示全名", detail(shortTitle));
        check(shortHeader > 0 && headerHeight(shortDlg) == shortHeader && headerHeight(midDlg) == shortHeader &&
                  shortTitle != nullptr && shortTitle->height() == shortTitleH,
              "LN13 页眉高度不随名字长短变",
              QStringLiteral("短 %1 / 改长后 %2 / 30 字 %3；名字行 %4 → %5")
                  .arg(shortHeader)
                  .arg(headerHeight(shortDlg))
                  .arg(headerHeight(midDlg))
                  .arg(shortTitleH)
                  .arg(shortTitle != nullptr ? shortTitle->height() : -1));
        shortDlg.setInstanceDisplayName(kMid);
        settle(app);
        check(shortTitle != nullptr && largestFit(shortTitle, lo, hi), "LN14 改回 12 字名：整名显示、提示清掉",
              detail(shortTitle));
    }
    return 0;
}
