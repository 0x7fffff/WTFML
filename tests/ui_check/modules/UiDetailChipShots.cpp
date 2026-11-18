// issue #20 的离屏样张：资源详情页顶排的大版本 chip 与下面默认收起的文件分组。
// 主题暮色雪山（dark）。用法：QT_QPA_PLATFORM=offscreen ui_check_detail_chip_shots <输出目录> [前缀]
// （紧凑运行器：ui_check_modules ui_check_detail_chip_shots <输出目录> [前缀]）。输出 <前缀>-<宽度>-<场景>.png，前缀缺省 shot。
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

// 一个热门模组常见的大版本表：26.x 到 1.7，再加最后的「其他」。
QList<ResourceDetail::Chip> makeChips() {
    QList<ResourceDetail::Chip> chips;
    ResourceDetail::Chip all;
    all.text = QStringLiteral("全部");
    chips.append(all);
    for (const QString &series :
         {QStringLiteral("26.2"), QStringLiteral("26.1"), QStringLiteral("1.21"), QStringLiteral("1.20"),
          QStringLiteral("1.19"), QStringLiteral("1.18"), QStringLiteral("1.17"), QStringLiteral("1.16"),
          QStringLiteral("1.15"), QStringLiteral("1.14"), QStringLiteral("1.12"), QStringLiteral("1.10"),
          QStringLiteral("1.8"), QStringLiteral("1.7")}) {
        ResourceDetail::Chip chip;
        chip.key = series;
        chip.text = series;
        chips.append(chip);
    }
    ResourceDetail::Chip other;
    other.key = QStringLiteral("other");
    other.text = QStringLiteral("其他");
    chips.append(other);
    return chips;
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

    // 三个场景：默认（全部）、点 1.21（下面只剩 1.21 的文件分组，全收起）、从实例进来预选 1.20.1
    // （顶排选 1.20，只有细版本正好是 1.20.1 的两组摊开）。
    const auto fillList = [](ResourceDetailPage &page, const QString &scene) {
        QList<ResourceDetail::Group> groups;
        const QStringList titles =
            scene == QLatin1String("all")
                ? QStringList{QStringLiteral("Fabric 26.2"), QStringLiteral("NeoForge 26.2"),
                              QStringLiteral("Fabric 1.21.4"), QStringLiteral("Fabric 1.21.1"),
                              QStringLiteral("NeoForge 1.21.1"), QStringLiteral("Fabric 1.20.1")}
            : scene == QLatin1String("series121")
                ? QStringList{QStringLiteral("Fabric 1.21.4"), QStringLiteral("Fabric 1.21.1"),
                              QStringLiteral("NeoForge 1.21.1")}
                : QStringList{QStringLiteral("NeoForge 1.20.1"), QStringLiteral("Fabric 1.20.1"),
                              QStringLiteral("Fabric 1.20")};
        for (const QString &title : titles) {
            ResourceDetail::Group group;
            group.title = title;
            for (int i = 0; i < 2; ++i) {
                ResourceDetail::Row row;
                row.key = title + QString::number(i);
                row.title = QStringLiteral("0.6.%1+mc1.21").arg(10 - i);
                row.subText = QStringLiteral("下载 120 万次, 更新于 3 天前");
                group.rows.append(row);
            }
            group.expanded = scene == QLatin1String("preselected") && title.endsWith(QLatin1String("1.20.1"));
            groups.append(group);
        }
        page.setGroups(groups);
    };

    // width = 详情页拿到的内容区宽度：窗口最小 900 时约 640，拉宽时约 1200。
    for (const int width : {640, 1200}) {
        for (const QString &scene : {QStringLiteral("all"), QStringLiteral("series121"),
                                     QStringLiteral("preselected")}) {
            ResourceDetailPage page;
            ResourceDetail::Header header;
            header.title = QStringLiteral("钠");
            header.description = QStringLiteral("现代化的渲染引擎，大幅提升帧率");
            header.metaLine = QStringLiteral("Fabric / NeoForge / 下载 2.31 亿次 / Modrinth");
            page.setHeader(header);
            page.setChips(makeChips(), scene == QLatin1String("series121")   ? QStringLiteral("1.21")
                                       : scene == QLatin1String("preselected") ? QStringLiteral("1.20")
                                                                               : QString());
            fillList(page, scene);
            page.resize(width, 640);
            page.show();
            pump(60);
            save(&page, QStringLiteral("%1-%2").arg(width).arg(scene));
        }
    }
    qInfo().noquote() << "detail chip shots:" << saved << "->" << out;
    return saved == 6 ? 0 : 1;
}
