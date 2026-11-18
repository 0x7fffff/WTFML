// issue #20 的离屏样张：资源详情页顶部那一排游戏版本 chip，按大版本分组前后的样子。
// 同一份源码在改前改后都能编（分组字段与组按钮按有没有来探测），所以改前图和改后图是同一个摆法。
// 主题暮色雪山（dark）。用法：QT_QPA_PLATFORM=offscreen ui_check_detail_chip_shots <输出目录>
// （紧凑运行器：ui_check_modules ui_check_detail_chip_shots <输出目录>）。输出 <前缀>-<场景>.png，前缀取第二个路径参数后面的词，缺省 shot。
// 不进 ctest：只出图，不做断言。
#include "Support.hpp"

#include "ResourceDetailPage.hpp"
#include "Theme.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>

namespace {

void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// 改前的 Chip 没有 group 字段：有就填，没有就什么都不做。
template <class C>
auto assignGroup(C &chip, const QString &group, int) -> decltype(chip.group = group, void()) {
    chip.group = group;
}
template <class C>
void assignGroup(C &, const QString &, long) {}

// 一个热门模组常见的声明表：1.8 到 26.x，再夹几条快照与预发布。
QList<ResourceDetail::Chip> makeChips() {
    const QStringList versions{
        QStringLiteral("26.2"),      QStringLiteral("26.1.1"),   QStringLiteral("26.1"),
        QStringLiteral("1.21.8"),    QStringLiteral("1.21.7"),   QStringLiteral("1.21.5"),
        QStringLiteral("1.21.4"),    QStringLiteral("1.21.1"),   QStringLiteral("1.21"),
        QStringLiteral("1.20.6"),    QStringLiteral("1.20.4"),   QStringLiteral("1.20.2"),
        QStringLiteral("1.20.1"),    QStringLiteral("1.19.4"),   QStringLiteral("1.19.2"),
        QStringLiteral("1.18.2"),    QStringLiteral("1.17.1"),   QStringLiteral("1.16.5"),
        QStringLiteral("1.15.2"),    QStringLiteral("1.14.4"),   QStringLiteral("1.12.2"),
        QStringLiteral("1.10.2"),    QStringLiteral("1.8.9"),    QStringLiteral("1.7.10"),
        QStringLiteral("24w14a")};
    QList<ResourceDetail::Chip> chips;
    ResourceDetail::Chip all;
    all.text = QStringLiteral("全部");
    chips.append(all);
    for (const QString &version : versions) {
        ResourceDetail::Chip chip;
        chip.key = version;
        chip.text = version;
        const bool plain = version.at(0).isDigit() && !version.contains(QLatin1Char('w'));
        assignGroup(chip, plain ? version.section(QLatin1Char('.'), 0, 1) : QStringLiteral("其他"), 0);
        chips.append(chip);
    }
    return chips;
}

void clickGroup(ResourceDetailPage &page, int index) {
    // 名字写字面量：改前的页面没有 chipGroupButtonNamePrefix()，找不到按钮就什么都不点。
    if (auto *button = page.findChild<QPushButton *>(QStringLiteral("resourceDetailChipGroup") +
                                                     QString::number(index))) {
        button->click();
    }
}

}  // namespace

int UiDetailChipShots(QApplication &app) {
    const QString fontPath = QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc"));
    const int fontId = QFile::exists(fontPath) ? QFontDatabase::addApplicationFont(fontPath) : -1;
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    app.setFont(QFont(families.isEmpty() ? QStringLiteral("Microsoft YaHei UI") : families.first(), 9));
    QString out = QDir::current().filePath(QStringLiteral("build/detail-chip-shots"));
    QString prefix = QStringLiteral("shot");
    const QStringList args = app.arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i).contains(QLatin1Char('/')) || args.at(i).contains(QLatin1Char('\\'))) {
            out = args.at(i);
            if (i + 1 < args.size()) prefix = args.at(i + 1);
        }
    }
    QDir().mkpath(out);
    Theme::setSpec(Theme::presetSpec(QStringLiteral("dark")));
    Theme::apply(app);
    Anim::setEnabled(false);

    int saved = 0;
    const auto save = [&](QWidget *w, const QString &name) {
        if (w->grab().save(QDir(out).filePath(QStringLiteral("%1-%2.png").arg(prefix, name)))) ++saved;
    };

    // 版本列表里放几行，让整页看着像真的详情页。
    const auto fillList = [](ResourceDetailPage &page) {
        QList<ResourceDetail::Group> groups;
        for (const QString &title : {QStringLiteral("Fabric 1.21.4"), QStringLiteral("Fabric 1.21.1"),
                                     QStringLiteral("NeoForge 1.21.1")}) {
            ResourceDetail::Group group;
            group.title = title;
            for (int i = 0; i < 2; ++i) {
                ResourceDetail::Row row;
                row.key = title + QString::number(i);
                row.title = QStringLiteral("0.6.%1+mc1.21").arg(10 - i);
                row.subText = QStringLiteral("下载 120 万次, 更新于 3 天前");
                group.rows.append(row);
            }
            groups.append(group);
        }
        page.setGroups(groups);
    };

    // width = 详情页拿到的内容区宽度：窗口最小 900 时约 640，拉宽时约 1200。
    for (const int width : {640, 1200}) {
        for (const QString &scene : {QStringLiteral("default"), QStringLiteral("expanded"),
                                     QStringLiteral("selected")}) {
            ResourceDetailPage page;
            ResourceDetail::Header header;
            header.title = QStringLiteral("钠");
            header.description = QStringLiteral("现代化的渲染引擎，大幅提升帧率");
            header.metaLine = QStringLiteral("Fabric / NeoForge / 下载 2.31 亿次 / Modrinth");
            page.setHeader(header);
            page.setChips(makeChips(),
                          scene == QLatin1String("selected") ? QStringLiteral("1.20.1") : QString());
            fillList(page);
            page.resize(width, 640);
            page.show();
            pump(60);
            if (scene == QLatin1String("expanded")) {
                clickGroup(page, 2);  // 顶排组的先后：26.2、26.1、1.21……
                pump(60);
            }
            save(&page, QStringLiteral("%1-%2").arg(width).arg(scene));
        }
    }
    qInfo().noquote() << "detail chip shots:" << saved << "->" << out;
    return saved == 6 ? 0 : 1;
}
