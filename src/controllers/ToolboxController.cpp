/**
 * @file ToolboxController.cpp
 * @brief ToolboxController 的实现（为什么这么分、哪几件事不归它管，见头文件那一段）。
 * @author 0x7fffff
 * @date 2026-10-16
 */

#include "controllers/ToolboxController.hpp"

#include "InlineMessage.hpp"
#include "MotdText.hpp"
#include "OverlayHost.hpp"
#include "ToolboxPage.hpp"
#include "tools/AnvilPlanner.hpp"
#include "tools/CoordConvert.hpp"
#include "tools/FileHash.hpp"
#include "tools/TextFormat.hpp"
#include "util/ServerPing.hpp"

#include <QColor>
#include <QMetaObject>
#include <QPalette>
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
/// ★ 颜色走 `MotdText::readableOn()`：游戏那边底色是黑的，服务器配的 `§f` 白字直接搬到
///   我们的浅底上等于没画。这一页是"看一眼长什么样"，所以浅底上读不清的那些**压暗到读得清**
///   —— 和服务器列表里那条 MOTD 是同一套做法（同一个函数，唯一出处）。
QString segmentsToHtml(const QList<TextFormat::Segment> &segments, const QColor &background) {
    QString html;
    for (const TextFormat::Segment &segment : segments) {
        if (segment.text.isEmpty()) {
            continue;
        }
        QColor color = QColor(TextFormat::colorRgb(segment.style.color));
        if (segment.style.color != TextFormat::Color::Default) {
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

    // ---------- 那四张表（各自的唯一出处都在 src/tools/ 里）----------
    QStringList algorithms;
    for (FileHash::Algorithm algorithm : FileHash::algorithms()) {
        algorithms.append(FileHash::algorithmName(algorithm));
    }
    m_page->setAlgorithmChoices(algorithms);
    m_page->setDimensionChoices(dimensionNames());

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

    // ---------- 五件工具的信号 ----------
    connect(m_page, &ToolboxPage::anvilPlanRequested, this, [this]() { runAnvilPlan(); });
    connect(m_page, &ToolboxPage::textSourceChanged, this, [this]() { refreshText(); });
    connect(m_page, &ToolboxPage::textJsonRequested, this,
            [this](const QString &json) { loadTextFromJson(json); });
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

    // 一进来先把文本生成器那一页的两块空着的地方填上（空预览框看着像没搭好）。
    refreshText();
}

ToolboxController::~ToolboxController() { shutdown(); }

void ToolboxController::shutdown() {
    m_hashAbort.storeRelaxed(1);
    if (m_hashThread != nullptr) {
        m_hashThread->wait();
    }
    if (m_pinger != nullptr) {
        m_pinger->abort();
    }
}

QStringList ToolboxController::dimensionNames() const {
    // ★ 能按比例换算的那三个（末地不在 8:1 那个关系里，但"从这一头看它在哪"这件事仍然
    //   要能问，所以它也列出来 —— 只是结果那一段会写明"末地不按比例算"）。
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
    if (m_page == nullptr || m_overlay == nullptr) {
        return;
    }
    bool known = false;
    const ToolboxPage::Tool tool = ToolboxPage::toolFromKey(toolKey, &known);
    if (!known) {
        return;  // 认不出来的键：不该发生（面板那五颗按钮的键就是这张表）
    }
    // 换一件工具**先换再开**：这一页是复用的对象，开出来那一刻已经是用户点的那一件。
    m_page->showTool(tool);
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
        Anvil::Unit unit;
        const QString id = Anvil::normalizeId(row.id);
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
                .arg(unitText(produced.result, true));
        }
        return QStringLiteral("（认不出来的那一件）");
    };
    int stepNumber = 0;
    for (const Anvil::Step &step : plan.steps) {
        ++stepNumber;
        QString line = QStringLiteral("第 %1 步：%2 + %3 → %4（代价 %5 级")
                           .arg(stepNumber)
                           .arg(nodeText(step, true), nodeText(step, false),
                                unitText(step.result, true))
                           .arg(step.cost.total);
        if (step.cost.penalties > 0) {
            line.append(QStringLiteral("，其中前科 %1").arg(step.cost.penalties));
        }
        if (step.cost.incompatible > 0) {
            line.append(QStringLiteral("，含不兼容 +%1").arg(step.cost.incompatible));
        }
        line.append(QChar(')'));
        view.steps.append(line);
    }
    view.summary = plan.steps.isEmpty()
                       ? tr("只有一件，不用合。")
                       : tr("一共 %1 级，合完是 %2。")
                             .arg(plan.totalCost)
                             .arg(unitText(plan.finalEnchantments, true).replace(
                                 QStringLiteral(" 的书"), QString()));
    view.anyTooExpensive = plan.anyTooExpensive;
    if (plan.anyTooExpensive) {
        view.notes.append(tr("★ 有计划里那一步的代价超过 39 级 —— 生存模式里铁砧会拒绝那一步"
                             "（创造模式不受这条限制）。换一个次序，或者先减掉几件上的前科。"));
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
    // ★ 认不出来的那几种组件**不猜**（`translate` 要查语言文件、`score` 要游戏在跑、
    //   `selector` 要挑实体 —— 都不是这一层能算的）。把名字列出来，用户自己知道少读了什么。
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
                if (m_host.log) {
                    m_host.log(result.ok
                                   ? tr("哈希：%1 = %2").arg(path, result.hex)
                                   : tr("哈希没算成：%1（%2）").arg(path, result.error));
                }
                // 线程自己收掉（这一趟结束了，下一趟要的是一条新的）。
                if (m_hashThread != nullptr) {
                    m_hashThread->deleteLater();
                    m_hashThread = nullptr;
                }
            },
            Qt::QueuedConnection);
    });
    m_hashThread->setParent(this);
    m_hashThread->start();
}

void ToolboxController::cancelHash() {
    m_hashAbort.storeRelaxed(1);
}
