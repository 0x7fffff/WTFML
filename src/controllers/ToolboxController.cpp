/**
 * @file ToolboxController.cpp
 * @brief ToolboxController 的实现（为什么这么分、哪几件事不归它管，见头文件那一段）。
 */

#include "controllers/ToolboxController.hpp"
#include "controllers/ArmorTrimPreviewController.hpp"
#include "controllers/DirectDownloadController.hpp"
#include "controllers/ToolboxInputState.hpp"

#include "InlineMessage.hpp"
#include "MotdText.hpp"
#include "OverlayHost.hpp"
#include "ToolboxPage.hpp"
#include "tools/AnvilPlanner.hpp"
#include "tools/ArmorTrim.hpp"
#include "tools/Banner.hpp"
#include "tools/Beacon.hpp"
#include "tools/BeaconBeam.hpp"
#include "tools/BannerRender.hpp"
#include "tools/BannerDesign.hpp"
#include "tools/BannerTextures.hpp"
#include "tools/Brewing.hpp"
#include "tools/CoordConvert.hpp"
#include "tools/FileHash.hpp"
#include "tools/TextFormat.hpp"
#include "util/ServerPing.hpp"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QMetaObject>
#include <QPalette>
#include <QSaveFile>
#include <QThread>

#include <algorithm>

namespace {

/// 一段附魔的显示名（`锋利 IV` —— 等级用罗马数字，和游戏里的写法和铁砧界面对得上）。
QString enchantmentText(const QString &id, int level) {
    static const char *kRoman[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    const QString roman = (level >= 1 && level <= 10) ? QString::fromLatin1(kRoman[level])
                                                      : QString::number(level);
    return QStringLiteral("%1 %2").arg(Anvil::enchantmentName(id), roman);
}

/// 一件东西的显示名（`锋利 III 的书` / `锋利 IV + 耐久 III 的装备`）。
QString unitText(const QMap<QString, int> &enchantments, bool isBook) {
    QStringList parts;
    for (auto it = enchantments.constBegin(); it != enchantments.constEnd(); ++it) {
        parts.append(enchantmentText(it.key(), it.value()));
    }
    const QString body = parts.isEmpty() ? QStringLiteral("（没有附魔）") : parts.join(QStringLiteral(" + "));
    return isBook ? QStringLiteral("%1 的书").arg(body) : QStringLiteral("%1 的装备").arg(body);
}

/// 文本生成器那三种输出里那一段/几段的 HTML（预览那一格画的）。
///
/// 颜色走 `MotdText::readableOn()`：游戏那边底色是黑的，服务器配的 `§f` 白字直接搬到
/// 我们的浅底上等于没画。这一页是"看一眼长什么样"，所以浅底上读不清的那些**压暗到读得清**
/// —— 和服务器列表里那条 MOTD 是同一套做法（同一个函数，唯一出处）。
QString segmentsToHtml(const QList<TextFormat::Segment> &segments, const QColor &background) {
    QString html;
    for (const TextFormat::Segment &segment : segments) {
        if (segment.text.isEmpty()) {
            continue;
        }
        // 自定色和那十六个一样，先按"在浅底上还读不读得清"压一档；没写过颜色才用正文色。
        const bool hasColor = segment.style.hasCustomColor
                              || segment.style.color != TextFormat::Color::Default;
        QColor color = QColor::fromRgb(TextFormat::styleRgb(segment.style));
        if (hasColor) {
            color = MotdText::readableOn(color, background);
        } else {
            color = background.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
        }
        QStringList styles;
        styles.append(QStringLiteral("color:%1").arg(color.name()));
        if (segment.style.bold) {
            styles.append(QStringLiteral("font-weight:bold"));
        }
        if (segment.style.italic) {
            styles.append(QStringLiteral("font-style:italic"));
        }
        QStringList decoration;
        if (segment.style.underline) {
            decoration.append(QStringLiteral("underline"));
        }
        if (segment.style.strikethrough) {
            decoration.append(QStringLiteral("line-through"));
        }
        if (!decoration.isEmpty()) {
            styles.append(QStringLiteral("text-decoration:%1").arg(decoration.join(QChar(' '))));
        }
        // 乱码那一档：游戏里那一段字会跳。**我们照原字画**（跳字在预览里只会让人看不出
        // 自己敲了什么），所以只把这件事在样式上带一笔颜色，不改字。
        if (segment.style.obfuscated) {
            styles.append(QStringLiteral("opacity:0.75"));
        }
        html.append(QStringLiteral("<span style='%1'>%2</span>")
                        .arg(styles.join(QChar(';')), segment.text.toHtmlEscaped()));
    }
    return html.isEmpty() ? QStringLiteral("<span style='opacity:0.6'>（这里还没有字）</span>")
                          : html;
}

/// 服务器 MOTD 那一行的 HTML（`§` 码 → 带色的 span；两行封顶，和游戏里一样）。
QString motdHtml(const QString &raw, const QColor &background) {
    const QVector<MotdPiece> pieces = MotdText::parse(raw, 2);
    QString html;
    int line = 0;
    for (const MotdPiece &piece : pieces) {
        while (line < piece.line) {
            html.append(QStringLiteral("<br>"));
            ++line;
        }
        const QColor color = MotdText::readableOn(MotdText::pieceColor(piece), background);
        QStringList styles;
        styles.append(QStringLiteral("color:%1").arg(color.name()));
        if (piece.bold) {
            styles.append(QStringLiteral("font-weight:bold"));
        }
        if (piece.italic) {
            styles.append(QStringLiteral("font-style:italic"));
        }
        QStringList decoration;
        if (piece.underline) {
            decoration.append(QStringLiteral("underline"));
        }
        if (piece.strike) {
            decoration.append(QStringLiteral("line-through"));
        }
        if (!decoration.isEmpty()) {
            styles.append(QStringLiteral("text-decoration:%1").arg(decoration.join(QChar(' '))));
        }
        html.append(QStringLiteral("<span style='%1'>%2</span>")
                        .arg(styles.join(QChar(';')), piece.text.toHtmlEscaped()));
    }
    return html;
}

}  // namespace

ToolboxController::ToolboxController(OverlayHost *overlay, QWidget *pageParent, Host host,
                                    QObject *parent)
    : QObject(parent), m_overlay(overlay), m_pageParent(pageParent), m_host(std::move(host)) {
    m_page = new ToolboxPage(pageParent);
    m_trimPreview = new ArmorTrimPreviewController(m_page, m_overlay, this);
    // 「显示披风」勾选只改预览那一张图，不碰资源、不写盘 —— 直接重绘即可。
    connect(m_page, &ToolboxPage::trimCapeVisibilityChanged, this,
            [this](bool) { m_trimPreview->refresh(); });
    // 算完哈希之后盯着那个文件：它一变，屏幕上那串摘要就不再是"它现在的摘要"。
    m_hashWatcher = new QFileSystemWatcher(this);
    connect(m_hashWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) { onHashFileChanged(path); });
    m_directDownload = new DirectDownloadController(m_host.activity, pageParent, this);
    connect(m_page, &ToolboxPage::downloadToolRequested, this, [this] { m_directDownload->open(); });
    QStringList beamNames;
    for (int i = 0; i < Banner::colorCount(); ++i) beamNames.append(Banner::colorName(i));
    m_page->setBeamColors(beamNames);
    connect(m_page, &ToolboxPage::beamRequested, this, [this](const QList<int> &glass) {
        const auto result = BeaconBeam::calculate(glass);
        QStringList rows;
        for (int i = 0; i < result.segments.size(); ++i)
            rows.append(tr("第 %1 层 · %2玻璃 → %3").arg(i + 1).arg(Banner::colorName(glass[i]), result.segments[i].name()));
        const QColor final = result.finalColor;
        rows.append(tr("最终颜色：%1 · RGB(%2, %3, %4)").arg(final.name()).arg(final.red()).arg(final.green()).arg(final.blue()));
        QStringList materials;
        for (int color = 0; color < Banner::colorCount(); ++color) {
            const int count = glass.count(color);
            if (count > 0) materials.append(tr("%1玻璃 × %2").arg(Banner::colorName(color)).arg(count));
        }
        if (!materials.isEmpty()) rows.append(tr("材料：%1").arg(materials.join(QStringLiteral("、"))));
        if (glass.isEmpty()) rows.prepend(tr("未放置染色玻璃：白色光柱。"));
        m_page->setBeamResult(result.segments, result.ok ? rows.join(QLatin1Char('\n')) : result.error);
    });
    m_page->beamRequested({});
    connect(m_page, &ToolboxPage::bannerTexturesRequested, this, [this] {
        if (m_bannerTextureLoading) return;
        const QString archive = QFileDialog::getOpenFileName(m_page, tr("选择原版游戏 JAR 或旗帜资源包"), {}, tr("游戏资源 (*.jar *.zip)"));
        if (archive.isEmpty()) return;
        m_bannerTextureLoading = true;
        m_page->setBannerPreview({}, tr("正在后台读取本地旗帜贴图…"));
        auto result = QSharedPointer<BannerTextures::Result>::create();
        auto *worker = QThread::create([result, archive] { *result = BannerTextures::load(archive); });
        connect(worker, &QThread::finished, this, [this, result] {
            m_bannerTextureLoading = false;
            if (!result->error.isEmpty()) { m_page->setBannerPreview({}, result->error); return; }
            m_bannerTextures = result->images;
            QList<QIcon> icons;
            for (int i = 0; i < Banner::patternCount(); ++i)
                icons.append(QIcon(QPixmap::fromImage(BannerRender::compose(0, {{i, 15}}, m_bannerTextures))));
            m_page->setBannerPatternIcons(icons);
            refreshBanner(m_page->bannerBaseColor(), m_page->bannerLayers());
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    });

    // ---------- 那四张表（各自的唯一出处都在 src/tools/ 里）----------
    QStringList algorithms;
    for (FileHash::Algorithm algorithm : FileHash::algorithms()) {
        algorithms.append(FileHash::algorithmName(algorithm));
    }
    m_page->setAlgorithmChoices(algorithms);
    m_page->setDimensionChoices(dimensionNames());

    // 信标 / 酿造那两张表（出处分别在 `Beacon` / `Brewing` 里，这一层只翻成字符串）。
    m_page->setBeaconMaterials(Beacon::materialNames());
    m_page->setBrewingPotions(Brewing::potionNames());
    QStringList brewForms;
    for (int index = 0; index < Brewing::formCount(); ++index) {
        brewForms.append(Brewing::formName(Brewing::Form(index)));
    }
    m_page->setBrewingForms(brewForms);

    // 旗帜 / 纹饰那两张表（出处分别在 `Banner` / `ArmorTrim` 里，这一层只翻成字符串）。
    QStringList bannerColors;
    for (int index = 0; index < Banner::colorCount(); ++index) {
        bannerColors.append(Banner::colorName(index));
    }
    m_page->setBannerColors(bannerColors);
    m_page->setBannerPatterns(Banner::patternNames());
    m_page->setBannerVersions(BannerDesign::versions());
    connect(m_page, &ToolboxPage::bannerSaveRequested, this, [this] {
        const QString path = QFileDialog::getSaveFileName(m_page, tr("保存旗帜设计"), QStringLiteral("banner.wtfml-banner.json"), tr("旗帜设计 (*.json)"));
        if (path.isEmpty()) return;
        QString error;
        if (!BannerDesign::save(path, {m_page->bannerBaseColor(), m_page->bannerVersion(), m_page->bannerLayers()}, &error))
            InlineMessage::information(m_overlay, tr("保存失败"), error);
    });
    connect(m_page, &ToolboxPage::bannerLoadRequested, this, [this] {
        const QString path = QFileDialog::getOpenFileName(m_page, tr("载入旗帜设计"), {}, tr("旗帜设计 (*.json)"));
        if (path.isEmpty()) return;
        BannerDesign::Design design; QString error;
        if (!BannerDesign::load(path, &design, &error)) {
            InlineMessage::information(m_overlay, tr("载入失败，当前设计未改变"), error); return;
        }
        m_page->setBannerDesign(design.base, design.layers, design.version);
        refreshBanner(design.base, design.layers);
    });

    QStringList trimArmor;
    for (int index = 0; index < ArmorTrim::armorMaterialCount(); ++index) {
        trimArmor.append(ArmorTrim::armorMaterialName(index));
    }
    m_page->setTrimArmorMaterials(trimArmor);
    QStringList trimParts;
    for (int index = 0; index < ArmorTrim::partCount(); ++index) {
        trimParts.append(ArmorTrim::partName(index));
    }
    m_page->setTrimParts(trimParts);
    m_page->setTrimPatterns(ArmorTrim::patternNames());
    m_page->setTrimMaterials(ArmorTrim::trimMaterialNames());

    QList<QPair<QString, QString>> enchantments;
    for (const Anvil::EnchantInfo &info : Anvil::enchantmentTable()) {
        enchantments.append({info.id, QStringLiteral("%1（%2，上限 %3 级）")
                                        .arg(info.name, info.english)
                                        .arg(info.maxLevel)});
    }
    m_page->setEnchantmentChoices(enchantments);

    QList<ToolboxPage::PaletteEntry> palette;
    for (char code = '0'; code <= '9'; ++code) {
        const QChar ch(code);
        const TextFormat::Color color = TextFormat::colorFromCode(ch);
        palette.append({QString(ch), TextFormat::colorText(color),
                        QColor(TextFormat::colorRgb(color)).name(), true});
    }
    for (char code = 'a'; code <= 'f'; ++code) {
        const QChar ch(code);
        const TextFormat::Color color = TextFormat::colorFromCode(ch);
        palette.append({QString(ch), TextFormat::colorText(color),
                        QColor(TextFormat::colorRgb(color)).name(), true});
    }
    // 五个格式码 + 复位（`k` 乱码、`l` 粗体、`m` 删除线、`n` 下划线、`o` 斜体、`r` 复位）。
    for (QChar ch : {QChar('k'), QChar('l'), QChar('m'), QChar('n'), QChar('o'), QChar('r')}) {
        palette.append({QString(ch), TextFormat::formatText(ch), QString(), false});
    }
    m_page->setTextPalette(palette);

    // ---------- 工具箱里那几件的信号 ----------
    connect(m_page, &ToolboxPage::anvilPlanRequested, this, [this]() { runAnvilPlan(); });
    connect(m_page, &ToolboxPage::textSourceChanged, this, [this]() { refreshText(); });
    connect(m_page, &ToolboxPage::textJsonRequested, this,
            [this](const QString &json) { loadTextFromJson(json); });
    connect(m_page, &ToolboxPage::textHexColorRequested, this, [this](const QString &hex) {
        quint32 rgb = 0;
        if (!TextFormat::parseHexColor(hex, &rgb)) {
            // 不猜：三位缩写、写错位数都直接说清楚要什么形状。
            m_page->setTextNotice(tr("自定颜色要写成 `#RRGGBB` 那种六位十六进制，"
                                     "比如 `#ff8800`（`#f80` 这种缩写游戏那边不认）。"));
            return;
        }
        m_page->insertTextAtCursor(TextFormat::legacyHexColor(rgb));
        m_page->setTextNotice(QString());
    });
    connect(m_page, &ToolboxPage::coordinateRequested, this,
            [this](int dimension, double x, double y, double z) {
                convertCoordinates(dimension, x, y, z);
            });
    connect(m_page, &ToolboxPage::teleportParseRequested, this,
            [this](const QString &text) { parseTeleport(text); });
    connect(m_page, &ToolboxPage::teleportCommandRequested, this,
            [this](int dimension, double x, double y, double z) {
                makeTeleportCommand(dimension, x, y, z);
            });
    connect(m_page, &ToolboxPage::serverCheckRequested, this,
            [this](const QString &address) { checkServer(address); });
    connect(m_page, &ToolboxPage::serverCheckCancelRequested, this,
            [this]() { cancelServerCheck(); });
    connect(m_page, &ToolboxPage::hashRequested, this,
            [this](const QString &path, int algorithm, const QString &expected) {
                runHash(path, algorithm, expected);
            });
    connect(m_page, &ToolboxPage::hashCancelRequested, this, [this]() { cancelHash(); });
    connect(m_page, &ToolboxPage::hashBatchRequested, this,
            [this](const QStringList &paths, int algorithm) { runHashBatch(paths, algorithm); });
    connect(m_page, &ToolboxPage::hashExportRequested, this,
            [this](const QString &path, const QString &text) { exportHashBatch(path, text); });
    connect(m_page, &ToolboxPage::beaconRequested, this,
            [this](int fromLevel, int level, int materialIndex, int beaconCount) {
                refreshBeacon(fromLevel, level, materialIndex, beaconCount);
            });
    connect(m_page, &ToolboxPage::brewingRequested, this,
            [this](int potion, bool extended, bool level2, int formIndex, int bottles) {
                refreshBrewing(potion, extended, level2, formIndex, bottles);
            });
    connect(m_page, &ToolboxPage::bannerRequested, this,
            [this](int baseColor, const QStringList &layers) { refreshBanner(baseColor, layers); });
    // 这几个信号还留着（隐藏的迁移适配器与验证工程用），但结果只看四列现在摆的装备。
    connect(m_page, &ToolboxPage::trimRequested, this, [this] { refreshTrim(); });
    connect(m_page, &ToolboxPage::trimEquipmentChanged, this, [this] { refreshTrim(); });

    ToolboxInputState::attach(m_page);
    // 一进来先把文本生成器那一页的两块空着的地方填上（空预览框看着像没搭好）。
    refreshText();
    // 信标、酿造也一样：开页之前先把那几格算出来，别摆在"空的"那一档上。
    refreshBeacon(m_page->beaconFromLevel(), m_page->beaconTargetLevel(),
                  m_page->beaconMaterialIndex(), m_page->beaconCount());
    refreshBrewing(m_page->brewingPotionIndex(), m_page->brewingExtended(),
                   m_page->brewingLevel2(), m_page->brewingFormIndex(), m_page->brewingBottles());
    refreshBanner(m_page->bannerBaseColor(), m_page->bannerLayers());
    refreshTrim();
}

ToolboxController::~ToolboxController() { shutdown(); }
void ToolboxController::setPlayerSkin(const QImage &skin, const QImage &cape, bool slim) {
    m_trimPreview->setPlayerSkin(skin, cape, slim);
}
bool ToolboxController::ownsDownloadTask(quint64 id) const { return m_directDownload->owns(id); }
bool ToolboxController::canCancelDownloadTask(quint64 id) const { return m_directDownload->canCancel(id); }
bool ToolboxController::cancelDownloadTask(quint64 id) { return m_directDownload->cancel(id); }
bool ToolboxController::pauseDownloadTask(quint64 id) { return m_directDownload->pause(id); }
bool ToolboxController::resumeDownloadTask(quint64 id) { return m_directDownload->resume(id); }

void ToolboxController::shutdown() {
    m_trimPreview->shutdown();
    m_directDownload->shutdown();
    m_hashAbort.storeRelaxed(1);
    if (m_hashThread != nullptr) {
        m_hashThread->wait();
    }
    // 指针可能已经被放掉了（见两处算哈希的收尾），而那条线程还在最后几条
    // 指令里。析构 QObject 会把子对象连同一条仍在跑的线程一起删掉 ⇒ qFatal。按子对象
    // 兜一遍底，保证收手时它们都已经退出。
    for (QThread *thread : findChildren<QThread *>()) {
        if (thread != m_hashThread) thread->wait();
    }
    if (m_pinger != nullptr) {
        m_pinger->abort();
    }
}

QStringList ToolboxController::dimensionNames() const {
    // 能按比例换算的那三个（末地不在 8:1 那个关系里，但"从这一头看它在哪"这件事仍然
    // 要能问，所以它也列出来 —— 只是结果那一段会写明"末地不按比例算"）。
    QStringList names;
    for (CoordConvert::Dimension dimension :
         {CoordConvert::Dimension::Overworld, CoordConvert::Dimension::Nether,
          CoordConvert::Dimension::End}) {
        names.append(CoordConvert::dimensionText(dimension));
    }
    return names;
}

int ToolboxController::dimensionAt(int index) const {
    switch (index) {
    case 0:
        return int(CoordConvert::Dimension::Overworld);
    case 1:
        return int(CoordConvert::Dimension::Nether);
    case 2:
        return int(CoordConvert::Dimension::End);
    default:
        return int(CoordConvert::Dimension::Unknown);
    }
}

void ToolboxController::open(const QString &toolKey) {
    if (toolKey == QStringLiteral("download")) { m_directDownload->open(); return; }
    if (m_page == nullptr || m_overlay == nullptr) {
        return;
    }
    bool known = false;
    const ToolboxPage::Tool tool = ToolboxPage::toolFromKey(toolKey, &known);
    if (!known) {
        return;  // 认不出来的键：不该发生（面板那六颗按钮的键就是这张表）
    }
    // 换一件工具**先换再开**：这一页是复用的对象，开出来那一刻已经是用户点的那一件。
    m_page->showTool(tool);
    if (tool == ToolboxPage::Tool::Trim) m_trimPreview->ensureLocal();
    // 「再开一页」不再问：覆盖层上压着东西时**静默收掉再继续**（`runWhenOverlayFree()`
    // 是那条路的唯一出处）。直接 execPage 的话，页正开着时它会原地返回 nullptr 那一档，
    // 用户看到的是"点了没反应"。
    const auto openPage = [this]() { m_overlay->execPage(m_page, ToolboxPage::windowTitle()); };
    if (m_overlay->isOpen()) {
        if (m_host.openWhenOverlayFree) {
            m_host.openWhenOverlayFree(openPage);
        }
        return;
    }
    openPage();
}

// ---------------------------------------------------------------------------
// 一、附魔顺序助手
// ---------------------------------------------------------------------------

void ToolboxController::runAnvilPlan() {
    const QList<ToolboxPage::AnvilRow> rows = m_page->anvilRows();
    if (rows.isEmpty()) {
        InlineMessage::information(m_overlay, tr("还没有东西要合"),
                                   tr("先在表里加一两行：选一条附魔、写上等级。"));
        return;
    }
    QList<Anvil::Unit> units;
    for (const ToolboxPage::AnvilRow &row : rows) {
        const QString id = Anvil::normalizeId(row.id);
        // 勾了「和上一行同一件」的，并进上一行那一件：一本书上能带好几条附魔，一件装备
        // 本来也可能已经有好几条 —— 它们不是"几件东西"，拆开算会把前科算重。
        if (row.sameAsPrevious && !units.isEmpty()) {
            Anvil::Unit &unit = units.last();
            // 同一件上写了两遍同一条附魔（手滑）：取高的那一档，别让低的那条把它盖回去。
            unit.enchantments.insert(id, qMax(unit.enchantments.value(id, 0), row.level));
            unit.label = unitText(unit.enchantments, unit.isBook);
            continue;
        }
        Anvil::Unit unit;
        unit.enchantments.insert(id, row.level);
        unit.penalty = Anvil::penaltyForUses(row.uses);
        unit.isBook = row.isBook;
        unit.label = unitText(unit.enchantments, unit.isBook);
        units.append(unit);
    }

    const Anvil::Plan plan = Anvil::plan(units);
    ToolboxPage::AnvilView view;
    view.ok = plan.ok;
    view.error = plan.error;
    if (!plan.ok) {
        m_page->setAnvilView(view);
        return;
    }
    // 每一步一行：`第 1 步：锋利 III 的书 + 锋利 III 的书 → 锋利 IV 的书（代价 3 级）`。
    const auto nodeText = [&units, &plan](const Anvil::Step &step, bool target) {
        const int index = target ? step.targetIndex : step.sacrificeIndex;
        if (index >= 0 && index < units.size()) {
            return units.at(index).label;
        }
        const int node = target ? step.targetNode : step.sacrificeNode;
        if (node >= 0 && node - units.size() < plan.steps.size()) {
            const Anvil::Step &produced = plan.steps.at(node - units.size());
            return QStringLiteral("第 %1 步合出来的那一件（%2）")
                .arg(node - units.size() + 1)
                .arg(unitText(produced.result, produced.resultIsBook));
        }
        return QStringLiteral("（认不出来的那一件）");
    };
    int stepNumber = 0;
    for (const Anvil::Step &step : plan.steps) {
        ++stepNumber;
        QString line = QStringLiteral("第 %1 步：左槽 %2 + 右槽 %3 → %4（代价 %5 级")
                           .arg(stepNumber)
                           .arg(nodeText(step, true), nodeText(step, false),
                                unitText(step.result, step.resultIsBook))
                           .arg(step.cost.total);
        if (step.cost.penalties > 0) {
            line.append(QStringLiteral("，其中前科 %1").arg(step.cost.penalties));
        }
        if (step.cost.incompatible > 0) {
            line.append(QStringLiteral("，含不兼容 +%1").arg(step.cost.incompatible));
        }
        line.append(QChar(')'));
        view.steps.append(line);
        // 互斥顶掉的那几条**点名说清楚**：不解释的话，用户会以为那一条悄悄丢了
        // （代价里那 +1 是照旧的，所以要说明"合不上来但还是要多花一级"）。
        for (auto it = step.cost.dropped.constBegin(); it != step.cost.dropped.constEnd(); ++it) {
            view.notes.append(tr("第 %1 步：「%2」和目标上已有的附魔互斥，这一步**没合上来** —— "
                                 "目标保住自己那条（铁砧按每一条互斥照旧 +1 级）。"
                                 "想留下这一条，就把它那一件放到左边先合。")
                                  .arg(stepNumber)
                                  .arg(enchantmentText(it.key(), it.value())));
        }
    }
    const QString finalText = unitText(plan.finalEnchantments, plan.finalIsBook)
                                  .replace(QStringLiteral(" 的书"), QString())
                                  .replace(QStringLiteral(" 的装备"), QString());
    view.summary = plan.steps.isEmpty()
                       ? tr("只有一件，不用合。")
                       : tr("一共 %1 级%2，合完是 %3。")
                             .arg(plan.totalCost)
                             .arg(plan.withinSurvival ? tr("（每一步都在 39 级以内）") : QString(),
                                  finalText);
    view.anyTooExpensive = plan.anyTooExpensive;
    if (plan.anyTooExpensive) {
        view.notes.append(tr("★ 有计划里那一步的代价超过 39 级 —— 生存模式里铁砧会拒绝那一步"
                             "（创造模式不受这条限制）。换一个次序，或者先减掉几件上的前科。"));
    }
    if (!plan.unused.isEmpty()) {
        QStringList leftover;
        for (int index : plan.unused) {
            if (index >= 0 && index < units.size()) {
                leftover.append(units.at(index).label);
            }
        }
        view.notes.append(tr("这几件**用不上**：%1 —— 加进去也升不了级（材料可以不全用）。")
                              .arg(leftover.join(QStringLiteral("、"))));
    }
    if (plan.finalPenalty > 0) {
        view.notes.append(tr("合完之后那一件的前科是 %1（= 在铁砧上用过 %2 次）。")
                              .arg(plan.finalPenalty)
                              .arg(Anvil::usesForPenalty(plan.finalPenalty)));
    }
    m_page->setAnvilView(view);
    if (m_host.log) {
        m_host.log(tr("附魔顺序助手：%1 件，合计 %2 级%3")
                       .arg(units.size())
                       .arg(plan.totalCost)
                       .arg(plan.anyTooExpensive ? tr("（其中一步超过 39 级）") : QString()));
    }
}

// ---------------------------------------------------------------------------
// 二、文本生成器
// ---------------------------------------------------------------------------

void ToolboxController::refreshText() {
    const QString raw = m_page->textSource();
    const QList<TextFormat::Segment> segments = TextFormat::parseLegacy(raw);
    const QColor background = m_page->palette().color(QPalette::Base);
    m_page->setTextPreview(segmentsToHtml(segments, background));
    m_page->setTextOutputs(TextFormat::toLegacy(segments), TextFormat::toJsonText(segments),
                           TextFormat::toTellrawCommand(segments));
    m_page->setTextMotd(TextFormat::toMotd(segments));
    m_page->setTextNotice(QString());
}

void ToolboxController::loadTextFromJson(const QString &json) {
    if (json.trimmed().isEmpty()) {
        m_page->setTextNotice(tr("「JSON 组件」那一格是空的 —— 先把一段 JSON 粘进去。"));
        return;
    }
    const TextFormat::JsonParseResult parsed = TextFormat::parseJson(json);
    if (!parsed.segments.isEmpty()) {
        m_page->setTextSource(TextFormat::toLegacy(parsed.segments));
    }
    if (parsed.unsupported.isEmpty()) {
        m_page->setTextNotice(tr("读回来了：%1 段。").arg(parsed.segments.size()));
        return;
    }
    // 认不出来的那几种组件**不猜**（`translate` 要查语言文件、`score` 要游戏在跑、
    // `selector` 要挑实体 —— 都不是这一层能算的）。把名字列出来，用户自己知道少读了什么。
    m_page->setTextNotice(tr("这几样没读：%1 —— 它们要游戏里那台服务器（或者语言文件）"
                             "才算得出来，这一层不猜。")
                              .arg(parsed.unsupported.join(QStringLiteral("、"))));
}

// ---------------------------------------------------------------------------
// 三、坐标换算
// ---------------------------------------------------------------------------

void ToolboxController::convertCoordinates(int dimensionIndex, double x, double y, double z) {
    const auto from = CoordConvert::Dimension(dimensionAt(dimensionIndex));
    const CoordConvert::Position position{x, y, z};
    QStringList rows;
    const auto show = [&](CoordConvert::Dimension to) {
        const CoordConvert::Position converted = CoordConvert::convert(from, to, position);
        rows.append(QStringLiteral("%1：%2")
                        .arg(CoordConvert::dimensionText(to),
                             CoordConvert::positionText(converted)));
    };
    rows.append(QStringLiteral("%1 里这一格：%2")
                    .arg(CoordConvert::dimensionText(from), CoordConvert::positionText(position)));
    for (CoordConvert::Dimension to :
         {CoordConvert::Dimension::Overworld, CoordConvert::Dimension::Nether,
          CoordConvert::Dimension::End}) {
        if (to == from) {
            continue;
        }
        if (CoordConvert::convertible(from, to)) {
            show(to);
        } else {
            rows.append(QStringLiteral("%1：末地不按比例算（两边坐标之间没有 8:1 那种关系）。")
                            .arg(CoordConvert::dimensionText(to)));
        }
    }
    // 区块与区域文件：只有主世界/下界这两套坐标能这么算（末地同样没有"比例"这回事，
    // 但区块是**每个维度各自**的 —— 这里给的是"手上这组坐标在它自己那个维度里的格子"）。
    const int chunkX = CoordConvert::blockToChunk(CoordConvert::floorToInt(position.x));
    const int chunkZ = CoordConvert::blockToChunk(CoordConvert::floorToInt(position.z));
    rows.append(QStringLiteral("区块：(x %1, z %2)（x %3、z %4）")
                    .arg(chunkX)
                    .arg(chunkZ)
                    .arg(CoordConvert::blockRangeText(chunkX), CoordConvert::blockRangeText(chunkZ)));
    const int regionX = CoordConvert::chunkToRegion(chunkX);
    const int regionZ = CoordConvert::chunkToRegion(chunkZ);
    rows.append(QStringLiteral("区域文件：%1")
                    .arg(CoordConvert::regionFileName(regionX, regionZ)));
    m_page->setCoordinateRows(rows);
}

void ToolboxController::parseTeleport(const QString &text) {
    const CoordConvert::ParsedTp parsed = CoordConvert::parseTeleportCommand(text);
    if (!parsed.ok) {
        m_page->setTeleportParse(false, parsed.error);
        return;
    }
    const auto dimension = parsed.dimension;
    // 认出来的是"哪一档维度"：说出来过就按下拉框里那一项选上（没说出来就**不动**那个框 ——
    // 用户自己挑的那个维度不该被悄悄改掉）。
    int index = -1;
    for (int candidate = 0; candidate < 3; ++candidate) {
        if (CoordConvert::Dimension(dimensionAt(candidate)) == dimension) {
            index = candidate;
        }
    }
    if (index >= 0) {
        m_page->setCoordinates(index, parsed.position.x, parsed.position.y, parsed.position.z);
    } else {
        // 没说维度的 `/tp @s 1 2 3`：坐标照填，维度那一格留在用户原来选的那一项上。
        m_page->setCoordinates(m_page->dimensionIndex(), parsed.position.x, parsed.position.y,
                               parsed.position.z);
    }
    QString message = tr("认出来了：%1")
                          .arg(CoordConvert::positionText(parsed.position));
    if (parsed.hasDimension) {
        message.append(tr("，维度 %1").arg(CoordConvert::dimensionText(parsed.dimension)));
    } else {
        message.append(tr("（这条命令里没写维度）"));
    }
    if (parsed.hasRotation) {
        message.append(tr("，朝向 yaw %1 / pitch %2")
                           .arg(parsed.yaw, 0, 'f', 2)
                           .arg(parsed.pitch, 0, 'f', 2));
    }
    m_page->setTeleportParse(true, message);
    convertCoordinates(m_page->dimensionIndex(), parsed.position.x, parsed.position.y,
                       parsed.position.z);
}

void ToolboxController::makeTeleportCommand(int dimensionIndex, double x, double y, double z) {
    const auto dimension = CoordConvert::Dimension(dimensionAt(dimensionIndex));
    const QString command =
        CoordConvert::toTeleportCommand(CoordConvert::Position{x, y, z}, dimension);
    m_page->setTeleportCommand(command);
}

// ---------------------------------------------------------------------------
// 四、服务器在线检测
// ---------------------------------------------------------------------------

void ToolboxController::checkServer(const QString &address) {
    if (address.isEmpty()) {
        InlineMessage::information(m_overlay, tr("先写个地址"),
                                   tr("比如 `mc.hypixel.net` 或者 `127.0.0.1:25565`。"));
        return;
    }
    if (m_pinger == nullptr) {
        m_pinger = new ServerPing::Pinger(this);
        connect(m_pinger, &ServerPing::Pinger::oneFinished, this,
                [this](int index, const ServerPing::Status &status) {
                    if (index != 0) {
                        return;  // 一次只问一台
                    }
                    ToolboxPage::ServerView view;
                    view.ok = status.ok;
                    view.timedOut = status.timedOut;
                    // 分类那句由这里翻成文字：`ui/` 不认识 `src/` 的枚举。
                    view.kind = ServerPing::kindName(status.kind);
                    view.error = status.ok ? QString() : status.error;
                    view.address = m_serverAddress;
                    if (status.ok) {
                        const QColor background = m_page->palette().color(QPalette::Base);
                        view.motd = motdHtml(status.motdRaw, background);
                        if (view.motd.isEmpty()) {
                            view.motd = status.motd.toHtmlEscaped();
                        }
                        view.version = status.versionName;
                        view.players = status.playersMax > 0
                                           ? QStringLiteral("%1 / %2")
                                                 .arg(status.playersOnline)
                                                 .arg(status.playersMax)
                                           : QStringLiteral("%1").arg(status.playersOnline);
                        view.latency = tr("%1 ms").arg(status.latencyMs);
                        if (!status.iconPng.isEmpty()) {
                            QPixmap icon;
                            if (icon.loadFromData(status.iconPng)) {
                                view.icon = icon;
                            }
                        }
                    }
                    m_page->setServerView(view);
                });
        connect(m_pinger, &ServerPing::Pinger::allFinished, this, [this]() {
            m_page->setServerBusy(false);
        });
    }
    m_serverAddress = address;
    m_page->setServerBusy(true);
    m_pinger->start({address});
}

void ToolboxController::cancelServerCheck() {
    if (m_pinger != nullptr) {
        m_pinger->abort();  // 手上那一台会问完（同步的 socket 掐不断），后面的不再问
    }
    m_page->setServerBusy(false);
}

// ---------------------------------------------------------------------------
// 五、文件哈希校验
// ---------------------------------------------------------------------------

void ToolboxController::runHash(const QString &path, int algorithmIndex, const QString &expected) {
    if (path.isEmpty()) {
        InlineMessage::information(m_overlay, tr("先选一个文件"),
                                   tr("把文件拖到这一页上，或者点「选一个…」。"));
        return;
    }
    if (m_hashThread != nullptr) {
        return;  // 已经有一条线程在算了（页面上那颗按钮这会儿也按不动）
    }
    const QList<FileHash::Algorithm> algorithms = FileHash::algorithms();
    if (algorithmIndex < 0 || algorithmIndex >= algorithms.size()) {
        algorithmIndex = 0;
    }
    const FileHash::Algorithm algorithm = algorithms.at(algorithmIndex);
    const FileHash::Expected want = FileHash::parseExpected(expected);
    m_hashAbort.storeRelaxed(0);
    m_page->setHashBusy(true);
    m_page->setHashStaleNote(QString());  // 新的一趟开始 ⇒ 上一趟那句"文件变过"收起来
    // 单文件那一趟开始，批量那本"盯着谁"的账也作废（屏幕上换成了这一次的结果）。
    if (m_hashWatcher != nullptr) {
        for (auto it = m_hashBatchStamps.constBegin(); it != m_hashBatchStamps.constEnd(); ++it) {
            m_hashWatcher->removePath(it.key());
        }
    }
    m_hashBatchStamps.clear();
    if (m_host.log) {
        m_host.log(tr("算哈希：%1（%2）").arg(path, FileHash::algorithmName(algorithm)));
    }

    m_hashThread = QThread::create([this, path, algorithm, want] {
        const auto progress = [this](qint64 done, qint64 total) {
            QMetaObject::invokeMethod(
                this,
                [this, done, total] {
                    if (m_page != nullptr) {
                        m_page->setHashProgress(done, total);
                    }
                },
                Qt::QueuedConnection);
        };
        const auto cancelled = [this]() { return m_hashAbort.loadRelaxed() != 0; };
        const FileHash::Result result = FileHash::digestOfFile(path, algorithm, progress, cancelled);
        QMetaObject::invokeMethod(
            this,
            [this, path, result, algorithm, want] {
                ToolboxPage::HashView view;
                view.ok = result.ok;
                view.error = result.error;
                view.algorithm = FileHash::algorithmName(algorithm);
                view.hex = result.hex;
                view.sizeText = FileHash::fileSizeText(result.bytes);
                if (result.ok && want.valid) {
                    const FileHash::Verdict verdict = FileHash::compare(result.hex, want);
                    view.hasVerdict = true;
                    view.matches = verdict.matches;
                    view.verdict = verdict.text;
                    // 已知值点的是**另一档**算法：比出来的"不一样"没有意义，得说清楚。
                    if (want.hasAlgorithm && want.algorithm != algorithm) {
                        view.verdict = tr("这一串是按 %1 算的，和上面选的 %2 不是一档 —— "
                                          "换个算法再算一遍。")
                                           .arg(FileHash::algorithmName(want.algorithm),
                                                FileHash::algorithmName(algorithm));
                        view.matches = false;
                    }
                }
                m_page->setHashView(view);
                m_page->setHashBusy(false);
                // 算成了就记下"这一刻文件长什么样"，并盯着它（文件变化使结果失效）。
                if (m_hashWatcher != nullptr && !m_hashWatchedPath.isEmpty()) {
                    m_hashWatcher->removePath(m_hashWatchedPath);
                }
                m_hashWatched = false;
                m_hashWatchedPath.clear();
                if (result.ok) {
                    const FileHash::Stamp stamp = FileHash::stampOf(path);
                    if (stamp.ok) {
                        m_hashWatchedPath = path;
                        m_hashWatchedBytes = stamp.bytes;
                        m_hashWatchedModifiedMs = stamp.lastModifiedMs;
                        m_hashWatched = true;
                        if (m_hashWatcher != nullptr) {
                            m_hashWatcher->addPath(path);
                        }
                    }
                }
                if (m_host.log) {
                    m_host.log(result.ok
                                   ? tr("哈希：%1 = %2").arg(path, result.hex)
                                   : tr("哈希没算成：%1（%2）").arg(path, result.error));
                }
                // 线程自己收掉（这一趟结束了，下一趟要的是一条新的）。
                // **只放指针、不 deleteLater**。这一句跑的时候那条 QThread 还在
                // `run()` 的尾巴上（`invokeMethod` 投完就返回）；对一条仍在跑的线程调
                // deleteLater() 会让 Qt 处理 DeferredDelete 时
                // `qFatal("QThread: Destroyed while thread is still running")`。
                // 线程对象自己接好了 finished → deleteLater（见下面创建处）。
                if (m_hashThread != nullptr) {
                    m_hashThread = nullptr;
                }
            },
            Qt::QueuedConnection);
    });
    m_hashThread->setParent(this);
    // 自己收自己；清指针排在删除之前（理由同单文件那一趟）。
    connect(m_hashThread, &QThread::finished, this, [this, thread = m_hashThread] {
        if (m_hashThread == thread) m_hashThread = nullptr;
    });
    connect(m_hashThread, &QThread::finished, m_hashThread, &QObject::deleteLater);
    m_hashThread->start();
}

void ToolboxController::cancelHash() {
    m_hashAbort.storeRelaxed(1);
}

void ToolboxController::runHashBatch(const QStringList &paths, int algorithmIndex) {
    if (paths.isEmpty() || m_hashThread != nullptr) {
        return;
    }
    const QList<FileHash::Algorithm> algorithms = FileHash::algorithms();
    if (algorithmIndex < 0 || algorithmIndex >= algorithms.size()) {
        algorithmIndex = 0;
    }
    const FileHash::Algorithm algorithm = algorithms.at(algorithmIndex);
    m_hashAbort.storeRelaxed(0);
    m_page->setHashBusy(true);
    m_page->setHashStaleNote(QString());
    // 上一趟（单文件或批量）盯着的那几个都作废 —— 屏幕上马上要换成这一次的清单。
    if (m_hashWatcher != nullptr) {
        if (!m_hashWatchedPath.isEmpty()) {
            m_hashWatcher->removePath(m_hashWatchedPath);
        }
        for (auto it = m_hashBatchStamps.constBegin(); it != m_hashBatchStamps.constEnd(); ++it) {
            m_hashWatcher->removePath(it.key());
        }
    }
    m_hashWatched = false;
    m_hashWatchedPath.clear();
    m_hashBatchStamps.clear();
    if (m_host.log) {
        m_host.log(tr("批量算哈希：%1 个文件（%2）")
                       .arg(paths.size())
                       .arg(FileHash::algorithmName(algorithm)));
    }

    m_hashThread = QThread::create([this, paths, algorithm] {
        QList<ToolboxPage::HashBatchRow> rows;
        QHash<QString, FileHash::Stamp> stamps;
        int failed = 0;
        bool aborted = false;
        for (int index = 0; index < paths.size(); ++index) {
            if (m_hashAbort.loadRelaxed() != 0) {
                aborted = true;
                break;
            }
            const QString path = paths.at(index);
            const QString name = QFileInfo(path).fileName();
            QMetaObject::invokeMethod(
                this,
                [this, index, count = paths.size(), name] {
                    if (m_page != nullptr) {
                        m_page->setHashProgressNote(tr("第 %1 / %2 个：%3")
                                                        .arg(index + 1)
                                                        .arg(count)
                                                        .arg(name));
                    }
                },
                Qt::QueuedConnection);
            const auto progress = [this](qint64 done, qint64 total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, done, total] {
                        if (m_page != nullptr) {
                            m_page->setHashProgress(done, total);
                        }
                    },
                    Qt::QueuedConnection);
            };
            const auto cancelled = [this]() { return m_hashAbort.loadRelaxed() != 0; };
            const FileHash::Result result =
                FileHash::digestOfFile(path, algorithm, progress, cancelled);
            ToolboxPage::HashBatchRow row;
            row.path = path;
            row.name = name;
            row.ok = result.ok;
            row.hex = result.hex;
            row.error = result.error;
            row.sizeText = FileHash::fileSizeText(result.bytes);
            if (result.ok) {
                const FileHash::Stamp stamp = FileHash::stampOf(path);
                if (stamp.ok) {
                    stamps.insert(path, stamp);
                }
            } else {
                ++failed;
            }
            rows.append(row);
            if (!result.ok && m_hashAbort.loadRelaxed() != 0) {
                aborted = true;
                break;
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, rows, stamps, failed, aborted, algorithm] {
                int okCount = 0;
                for (const ToolboxPage::HashBatchRow &row : rows) {
                    if (row.ok) {
                        ++okCount;
                    }
                }
                QString summary = tr("共 %1 个文件 · %2")
                                      .arg(rows.size())
                                      .arg(FileHash::algorithmName(algorithm));
                if (aborted) {
                    summary += tr(" · 停下了，后面那几个没算");
                }
                if (failed > 0) {
                    summary += tr(" · %1 个没算成").arg(failed);
                }
                if (okCount == 0) {
                    summary += tr(" · 一个都没算成");
                }
                m_page->setHashBatchView(rows, summary);
                m_page->setHashBusy(false);
                // 算成功的那些逐个盯着（文件变化使结果失效，批量这一档也一样）。
                m_hashBatchStamps = stamps;
                if (m_hashWatcher != nullptr) {
                    for (auto it = m_hashBatchStamps.constBegin();
                         it != m_hashBatchStamps.constEnd(); ++it) {
                        m_hashWatcher->addPath(it.key());
                    }
                }
                if (m_host.log) {
                    m_host.log(aborted ? tr("批量算哈希：停下了（%1 / %2）")
                                             .arg(rows.size())
                                             .arg(stamps.size() + failed)
                                       : tr("批量算哈希：%1 个文件，%2 个没算成")
                                             .arg(rows.size())
                                             .arg(failed));
                }
                if (m_hashThread != nullptr) {
                    m_hashThread = nullptr;
                }
            },
            Qt::QueuedConnection);
    });
    m_hashThread->setParent(this);
    // 自己收自己；清指针排在删除之前（`shutdown()` 里那句 wait() 在析构时跑）。
    connect(m_hashThread, &QThread::finished, this, [this, thread = m_hashThread] {
        if (m_hashThread == thread) m_hashThread = nullptr;
    });
    connect(m_hashThread, &QThread::finished, m_hashThread, &QObject::deleteLater);
    m_hashThread->start();
}

void ToolboxController::exportHashBatch(const QString &path, const QString &text) {
    if (path.isEmpty() || text.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        InlineMessage::information(m_overlay, tr("没写成"),
                                   tr("「%1」打不开：%2").arg(QDir::toNativeSeparators(path),
                                                             file.errorString()));
        return;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        InlineMessage::information(m_overlay, tr("没写成"),
                                   tr("「%1」没写完：%2").arg(QDir::toNativeSeparators(path),
                                                             file.errorString()));
        return;
    }
    if (m_host.log) {
        m_host.log(tr("哈希清单导出：%1").arg(path));
    }
    InlineMessage::information(m_overlay, tr("存好了"),
                               tr("清单写到 %1").arg(QDir::toNativeSeparators(path)));
}

void ToolboxController::onHashFileChanged(const QString &path) {
    if (m_page == nullptr) {
        return;
    }
    // ---------- 批量那一档：清单里哪个文件变了 ----------
    auto batch = m_hashBatchStamps.constFind(path);
    if (batch != m_hashBatchStamps.constEnd()) {
        const FileHash::Stamp before = batch.value();
        const FileHash::Stamp now = FileHash::stampOf(path);
        if (now.ok && now.bytes == before.bytes && now.lastModifiedMs == before.lastModifiedMs) {
            // 只是被别的程序碰了一下（读打开之类的通知），文件本身没变 —— 继续盯着。
            if (m_hashWatcher != nullptr) {
                m_hashWatcher->addPath(path);
            }
            return;
        }
        // 这一条只提醒一次：从"盯着的"表里摘掉，免得每存一次盘就再弹一遍同一句话。
        m_hashBatchStamps.erase(batch);
        if (m_hashWatcher != nullptr) {
            m_hashWatcher->removePath(path);
        }
        const QString name = QFileInfo(path).fileName();
        m_page->setHashStaleNote(
            now.ok ? tr("「%1」在算完之后变过（大小 / 修改时间对不上）—— 清单里那一串不是它"
                       "现在的摘要了，重新算一遍。")
                         .arg(name)
                   : tr("「%1」在算完之后被移走或删掉了 —— 清单里那一串不是它现在的样子。")
                         .arg(name));
        return;
    }
    // ---------- 单文件那一档 ----------
    if (!m_hashWatched || path != m_hashWatchedPath) {
        return;
    }
    // 这一发只提醒一次：先把"盯着"这件事收掉，免得用户每存一次盘就再弹一遍同一句话。
    m_hashWatched = false;
    if (m_hashWatcher != nullptr) {
        m_hashWatcher->removePath(m_hashWatchedPath);
    }
    const FileHash::Stamp now = FileHash::stampOf(path);
    if (!now.ok) {
        m_page->setHashStaleNote(tr("这个文件算完之后被移走或删掉了 —— 上面那串摘要不是它现在的样子。"));
        return;
    }
    if (now.bytes == m_hashWatchedBytes && now.lastModifiedMs == m_hashWatchedModifiedMs) {
        // 只是被别的程序碰了一下（读打开之类的通知），文件本身没变 —— 继续盯着。
        m_hashWatched = true;
        if (m_hashWatcher != nullptr) {
            m_hashWatcher->addPath(path);
        }
        return;
    }
    m_page->setHashStaleNote(
        tr("这个文件在算完之后变过（大小 / 修改时间对不上）—— 上面那串摘要不是它现在的样子，再算一遍。"));
}

// ---------------------------------------------------------------------------
// 六、信标计算 / 七、酿造助手
// ---------------------------------------------------------------------------

void ToolboxController::refreshBeacon(int fromLevel, int level, int materialIndex,
                                     int beaconCount) {
    if (m_page == nullptr) {
        return;
    }
    // 两格都是转盘（0–4 / 1–4），越不了界；真给了怪值也由 `Beacon` 回一句人话。
    //
    // 后面两行是两块：矿物折算（方块 → 锭/宝石）与多颗信标共用
    // 底座。`detailText()` 那段本身不碰材质 —— 材质只影响折算，效果和层数跟它无关。
    QString text = Beacon::detailText(fromLevel, level);
    const int baseBlocks = Beacon::sharedBaseBlocks(level, beaconCount);
    text += QStringLiteral("\n\n") + Beacon::materialText(materialIndex, baseBlocks);
    text += QStringLiteral("\n") + Beacon::sharedBaseText(level, beaconCount);
    m_page->setBeaconResult(text);
}

void ToolboxController::refreshBrewing(int potionIndex, bool extended, bool level2,
                                       int formIndex, int bottles) {
    if (m_page == nullptr) {
        return;
    }
    Brewing::Form form = Brewing::Form::Normal;
    if (formIndex >= 0 && formIndex < Brewing::formCount()) {
        form = Brewing::Form(formIndex);
    }
    m_page->setBrewingResult(
        Brewing::plan(potionIndex, extended, level2, form, bottles).detailText());
}

void ToolboxController::refreshBanner(int baseColor, const QStringList &layers) {
    if (m_page == nullptr) {
        return;
    }
    // 页面上每层是 `图案下标:颜色下标`；认不出来的那一段原样变成 -1，由 `Banner` 回一句人话。
    QList<Banner::Layer> parsed;
    for (const QString &entry : layers) {
        const QStringList parts = entry.split(QChar(':'));
        Banner::Layer layer;
        layer.patternIndex = parts.value(0).toInt();
        layer.colorIndex = parts.value(1).toInt();
        parsed.append(layer);
    }
    const Banner::Plan plan = Banner::plan(baseColor, parsed);
    QList<bool> availability;
    const QString version = m_page->bannerVersion();
    for (int i = 0; i < Banner::patternCount(); ++i) availability.append(BannerDesign::available(i, version));
    m_page->setBannerPatternAvailability(availability);
    QStringList unsupported;
    for (const auto &layer : parsed) {
        if (!BannerDesign::available(layer.patternIndex, version)) unsupported.append(Banner::patternAt(layer.patternIndex).name);
    }
    m_page->setBannerResult((unsupported.isEmpty() ? QString() : tr("目标版本 %1 不支持：%2。已保留图层，请调整设计或目标版本。\n\n").arg(version, unsupported.join(QStringLiteral("、")))) + plan.detailText());
    m_page->setBannerCommand(plan.ok && unsupported.isEmpty() ? plan.command : QString());
    QString notice;
    const auto image = plan.ok ? BannerRender::compose(baseColor, parsed, m_bannerTextures, &notice) : QImage();
    if (!plan.ok) notice = plan.error;
    if (m_bannerTextures.isEmpty() && parsed.isEmpty()) notice = tr("加载本地游戏贴图后可预览图案；当前仅显示底色。需要重新加载时可再次选择 JAR。");
    m_page->setBannerPreview(image, notice);
}

void ToolboxController::refreshTrim() {
    if (m_page == nullptr) {
        return;
    }
    QStringList equipped, notes;
    QMap<QString, int> materials;
    QString error;
    const auto equipment = m_page->trimEquipment();
    for (int slot = 0; slot < equipment.size(); ++slot) {
        const auto selection = equipment[slot];
        if (selection.armor < 0) {
            continue;
        }
        const auto armor = ArmorTrim::armorPieceName(selection.armor, slot);
        if (armor.isEmpty() || !ArmorTrim::armorMaterialFitsPart(selection.armor, slot)) {
            continue;
        }
        materials[armor]++;
        if (selection.pattern < 0) {
            equipped.append(armor);
            continue;
        }
        const auto pattern = ArmorTrim::patternAt(selection.pattern);
        const auto material = ArmorTrim::trimMaterialAt(selection.material);
        equipped.append(tr("%1：%2 · %3").arg(armor, pattern.name, material.name));
        materials[pattern.name + tr("纹饰模板")]++;
        materials[material.item]++;
        // 提示（这枚模板从哪来 / 哪个版本才有 / 模板可以复制）只从判据层拿：
        // 每件各算一条单件计划，只取它那几条 notes，材料账仍按四列合起来算。
        const auto plan = ArmorTrim::plan(selection.armor, slot, selection.pattern,
                                          selection.material, 1);
        if (!plan.ok) {
            error = plan.error;
            break;
        }
        for (const QString &note : plan.notes) {
            if (!notes.contains(note)) {
                notes.append(note);
            }
        }
    }
    if (!error.isEmpty()) {
        m_page->setTrimResult(error);
        m_trimPreview->refresh();
        return;
    }
    if (equipped.isEmpty()) {
        m_page->setTrimResult(tr("尚未选择装备"));
        m_trimPreview->refresh();
        return;
    }
    QStringList items;
    for (auto it = materials.cbegin(); it != materials.cend(); ++it) {
        items.append(tr("%1 × %2").arg(it.key()).arg(it.value()));
    }
    QString text = equipped.join(QChar('\n')) + tr("\n\n材料：")
                   + items.join(QStringLiteral(" · "));
    if (!notes.isEmpty()) {
        text += tr("\n\n提示：\n");
        for (const QString &note : notes) {
            text += tr("· %1\n").arg(note);
        }
        text.chop(1);
    }
    m_page->setTrimResult(text);
    m_trimPreview->refresh();
}
