/**
 * @file ToolboxPage.hpp
 * @brief 工具箱页：主窗口的第六个选项卡，导航轨底部「工具箱」进来。目录页 → 工具页两层。
 * @note 九件工具：附魔助手、坐标换算、信标计算、酿造助手、彩色文本、旗帜设计、纹饰搭配、
 * 高速下载、延迟测试。
 *
 * 【两层】目录页是自适应网格（每格图标 + 名称 + 一句用途），点一格进工具页；工具页页头是
 * 面包屑「工具箱 / 附魔助手」：「工具箱」点了回目录，工具名点了弹浮层（`GlassPopover`）直接
 * 换到别的工具。**没有页内页签，也没有导航轨底部的弹出框。**
 *
 * 【每件工具一片】十片各自一层滚动区，放在 `QStackedWidget` 里换着露。构造时只搭页头和目录页；
 * 某件工具第一次进去才搭那一片（`showTool()` → `ensureTool()`），搭完发 `toolBuilt()`，之后一直
 * 留着，再进只换格。**某片没搭之前，它那一组 set / get 都不能调** —— 灌表、恢复输入、算初值一律
 * 接 `toolBuilt()` 做。窄窗口下每片自己重排（内容宽 < 640 时折行 / 叠放），不出横向滚动条。
 *
 * 【这一页自己一根手指都不碰游戏目录】
 *
 * `ui/` **不 include `src/`**（全仓的规矩）。这一页只干两件事：把用户敲的东西**收成信号**
 * 发出去、把 `ToolboxController` 算好的**字符串**摆上去。算哈希、认坐标、拼 `tellraw`、排附魔
 * 顺序全在 `src/tools/`（纯函数，各自有判据）。哈希要读大文件、服务器检测要联网，这两件是慢活，
 * 由 controller 派到线程上去。「高速下载」整条链在 `DirectDownloadController` 里，这一片只管
 * 收输入、摆状态（`DownloadToolPanel`）。
 */

#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QPixmap>
#include <QWidget>

class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDropEvent;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QStackedWidget;
class QScrollArea;
class QPushButton;
class DownloadToolPanel;
class ArmorTrimPreview;
namespace SkinModel { struct Scene; }

class ToolboxPage : public QWidget {
    Q_OBJECT

public:
    /// 十件工具。**`tools()` 是目录页格子与切换浮层的次序唯一出处。**
    enum class Tool { Anvil, Text, Coord, Server, Hash, Beacon, Brewing, Banner, Trim, Download };

    /// 工具的键（`ToolboxController::open()` 用它认人）。
    static QString toolKey(Tool tool);
    /// 反着来。认不出来时 @p ok 写 false。
    static Tool toolFromKey(const QString &key, bool *ok = nullptr);
    /// 名字（`附魔助手`）：目录格、面包屑、切换浮层都用它。
    static QString toolName(Tool tool);
    /// 目录格里的一句用途（≤14 字）。
    static QString toolSummary(Tool tool);
    /// 十件，**次序唯一出处**（目录页格子、切换浮层）。
    static QList<Tool> tools();
    /// 一枚 24×24 线稿图标。**不是位图资源** —— 现画，颜色由调用方给。
    static QIcon toolIcon(Tool tool, const QColor &ink);
    /// 页头那个「工具箱」。
    static QString pageTitle() { return tr("工具箱"); }

    /// 验证工程认控件用的名字：目录页里那一格 / 工具页（滚动区）/ 工具面板本体。
    static QString toolCellName(Tool tool);
    static QString toolPanelName(Tool tool);
    /// 面板外面那层滚动容器 —— **它才是 `QStackedWidget` 的那一页**。
    static QString toolPageName(Tool tool);
    /// 页头面包屑上的三个控件。
    static QString crumbRootName() { return QStringLiteral("toolboxCrumbRoot"); }
    static QString crumbToolName() { return QStringLiteral("toolboxCrumbTool"); }
    static QString catalogName() { return QStringLiteral("toolboxCatalog"); }
    static QString switchPopoverName() { return QStringLiteral("toolboxSwitchPopover"); }

    /// 信标 / 酿造 / 旗帜 / 纹饰四件里认控件用的名字（验证工程用）。
    static QString beaconFromSpinName() { return QStringLiteral("toolboxBeaconFrom"); }
    static QString beaconLevelSpinName() { return QStringLiteral("toolboxBeaconLevel"); }
    /// 底座方块种类（铁块 / 金块 / 钻石块 / 绿宝石块 / 下界合金块）。
    static QString beaconMaterialComboName() { return QStringLiteral("toolboxBeaconMaterial"); }
    /// 同一座金字塔上放几颗信标（1–9）。
    static QString beaconCountSpinName() { return QStringLiteral("toolboxBeaconCount"); }
    static QString beaconResultName() { return QStringLiteral("toolboxBeaconResult"); }
    static QString brewingPotionComboName() { return QStringLiteral("toolboxBrewingPotion"); }
    static QString brewingExtendedName() { return QStringLiteral("toolboxBrewingExtended"); }
    static QString brewingLevel2Name() { return QStringLiteral("toolboxBrewingLevel2"); }
    static QString brewingFormComboName() { return QStringLiteral("toolboxBrewingForm"); }
    /// 要几瓶（1–64；一次酿造最多 3 瓶）。
    static QString brewingBottlesSpinName() { return QStringLiteral("toolboxBrewingBottles"); }
    /// 目标版本（空串 = 不限版本；判据在 `GameVersion`）。
    static QString brewingVersionComboName() { return QStringLiteral("toolboxBrewingVersion"); }
    static QString brewingResultName() { return QStringLiteral("toolboxBrewingResult"); }
    static QString bannerBaseComboName() { return QStringLiteral("toolboxBannerBase"); }
    static QString bannerPatternComboName() { return QStringLiteral("toolboxBannerPattern"); }
    static QString bannerColorComboName() { return QStringLiteral("toolboxBannerColor"); }
    static QString bannerAddName() { return QStringLiteral("toolboxBannerAdd"); }
    static QString bannerListName() { return QStringLiteral("toolboxBannerList"); }
    static QString bannerUpName() { return QStringLiteral("toolboxBannerUp"); }
    static QString bannerDownName() { return QStringLiteral("toolboxBannerDown"); }
    static QString bannerRemoveName() { return QStringLiteral("toolboxBannerRemove"); }
    static QString bannerUndoName() { return QStringLiteral("toolboxBannerUndo"); }
    static QString bannerResultName() { return QStringLiteral("toolboxBannerResult"); }
    static QString trimArmorComboName() { return QStringLiteral("toolboxTrimArmor"); }
    static QString trimPartComboName() { return QStringLiteral("toolboxTrimPart"); }
    static QString trimPatternComboName() { return QStringLiteral("toolboxTrimPattern"); }
    static QString trimMaterialComboName() { return QStringLiteral("toolboxTrimMaterial"); }
    static QString trimPiecesSpinName() { return QStringLiteral("toolboxTrimPieces"); }
    /// 「目标版本」下拉框（纹饰与旗帜 / 酿造同一套版本表）。
    static QString trimVersionComboName() { return QStringLiteral("toolboxTrimVersion"); }
    /// 「整套（四件）」那个勾（「单件 / 整套预览」）。
    static QString trimWholeSetName() { return QStringLiteral("toolboxTrimWholeSet"); }
    static QString trimResultName() { return QStringLiteral("toolboxTrimResult"); }
    /// 「显示披风」那个勾（默认不勾；没有披风的账号那一格是灰的）。
    static QString trimCapeCheckName() { return QStringLiteral("toolboxTrimCape"); }
    /// 哈希那一件：算完之后文件被改过的那句提醒（空串 = 收起来）。
    static QString hashNoticeName() { return QStringLiteral("toolboxHashNotice"); }
    static QString hashCopyAllName() { return QStringLiteral("toolboxHashCopyAll"); }
    static QString hashExportName() { return QStringLiteral("toolboxHashExport"); }

    explicit ToolboxPage(QWidget *parent = nullptr);

    /// 进 @p tool 那一片（开页时也走它）。
    void showTool(Tool tool);
    Tool currentTool() const { return m_current; }
    /// 回目录页。
    void showCatalog();
    /// 现在停在目录页吗。
    bool inCatalog() const { return m_inCatalog; }
    /// @p tool 那一片搭出来了吗（第一次 `showTool()` 才搭）。
    bool hasTool(Tool tool) const { return m_pages.contains(tool); }
    /// 「高速下载」那一片（`DirectDownloadController::attach()` 接它；没进过那一件时是空指针）。
    DownloadToolPanel *downloadPanel() const { return m_download; }
    /// 工具页里内容区的最大宽度（宽窗口时居中）。
    static constexpr int kToolMaxWidth = 880;
    static constexpr int kCatalogMaxWidth = 980;

    // ================= 一、附魔顺序助手 =================

    /// 一行输入：一条附魔 + 它的等级 + 这一件是附魔书还是已经附过魔的装备 + 在铁砧上用过几次。
    struct AnvilRow {
        QString id;      ///< 附魔 id（下拉框里那一项的 data）
        int level = 1;
        bool isBook = true;
        int uses = 0;    ///< 在铁砧上加工过几次（前科 = `2^n − 1`，界面上的小字会写出来）
        /// 这一条和**上一行**说的是同一件东西（一本书上带好几条附魔 / 一件装备上本来就有
        /// 好几条）。第一行没有"上一行"，这一格恒为 false。
        bool sameAsPrevious = false;
    };
    /// 算完之后摆上去的那一份。
    struct AnvilView {
        bool ok = false;
        QString error;        ///< 非空 = 算不了（表里没有那条附魔、超过 10 件…）
        QStringList steps;    ///< 每一步一行（`第 1 步：锋利 III 的书 → 锋利 IV 的书（3 级）`）
        QString summary;      ///< 一句总述（`一共 7 级，合完是锋利 V + 耐久 III`）
        QStringList notes;    ///< 要提醒的（超 39 级那一条等）
        bool anyTooExpensive = false;
    };

    /// 下拉框里那几项（id → 中文名）。**表在 `Anvil::enchantmentTable()`**，这一页不另写一份。
    void setEnchantmentChoices(const QList<QPair<QString, QString>> &idAndName);
    /// 现在表格里那几行（用户点了「算」）。
    QList<AnvilRow> anvilRows() const;
    /// 勾了「顺带改个名」没有（那 1 级算进去；空着 = 不改名）。
    bool anvilRename() const;
    void setAnvilRows(const QList<AnvilRow> &rows);
    /// 把结论摆上去。
    void setAnvilView(const AnvilView &view);

    // ================= 二、文本生成器 =================

    /// 色码 / 字形那一排上的一颗（**表在 `TextFormat` 里**：十六色那张表和五个格式码，
    /// 这一页一个字节都不抄）。
    struct PaletteEntry {
        QString code;     ///< 那个码位（`0`~`f` 是颜色、`l` / `o` / `n` / `m` / `k`、`r` 复位）
        QString name;     ///< 中文名（按钮的 tooltip）
        QString rgbHex;   ///< 颜色码的色（`#RRGGBB`）；格式码那几颗留空
        bool isColor = false;
    };

    /// 摆出那一排色码 / 字形按钮（`Label` 里那个 `source` 那一格要的工具条）。
    void setTextPalette(const QList<PaletteEntry> &entries);
    /// 用户粘进来的那串原文（可能带 `§` 码）。
    QString textSource() const;
    /// 把原文换成 @p text（「从 JSON 读回来」那一条把读出来的段写回这一格）。**会发
    /// `textSourceChanged()`** —— 正是要让 controller 照新内容重算一遍预览和三种输出。
    void setTextSource(const QString &text);
    /// 预览那一块（富文本 HTML；颜色和字形都由 controller 算好）。
    void setTextPreview(const QString &html);
    struct TextRun {
        QString text;
        QString colorCode;
        QColor color;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        bool obfuscated = false;
    };
    void setTextPreviewRuns(const QList<TextRun> &runs);
    QList<TextRun> textPreviewRuns() const;
    void applyTextPreviewColor(const QColor &color, const QString &code);
    /// 源码区：`§` 写法、JSON 组件。空指针语义的 QString 保留现有 § 输入。
    void setTextOutputs(const QString &legacy, const QString &json);
    /// `server.properties` 那一整行 `motd=…`（controller 已经把非 ASCII 转成
    /// `\uXXXX` 了 —— 按 ISO-8859-1 读也不会坏）。
    void setTextMotd(const QString &motd);
    /// 在原文那个光标处插一段（「插自定颜色」那颗按的：`§x§R§R§G§G§B§B` 是 controller
    /// 按 `TextFormat` 的规矩算好递进来的，这一页不自己拼）。
    void insertTextAtCursor(const QString &text);
    /// 认不出来的那几种组件（`translate`、`score`、写错的 `#RRGGBB`）——**不猜**，就把这句摆出来。
    void setTextNotice(const QString &notice);

    // ================= 三、坐标换算 =================

    /// 那一排维度（`主世界` / `下界` / `末地`；次序由 controller 给，页面上不写死）。
    void setDimensionChoices(const QStringList &names);
    /// 从维度下拉框里选的是第几项；坐标那三格；以及要不要把结果复制出去。
    int dimensionIndex() const;
    /// 三格坐标（用户敲的 / 从 tp 命令里认出来的都在里面）。
    double coordinate(int axis) const;  ///< @p axis：0 = x、1 = y、2 = z
    /// 把三格坐标与维度摆成"这一趟问的那一组"（认出来 tp 命令之后由 controller 调）。
    void setCoordinates(int dimensionIndex, double x, double y, double z);
    /// 结果那几行（`下界：x 10.00 y 64.00 z -9.50`、`区块 (0, -1) 一带`、`region 文件 r.0.-1.mca`…）。
    void setCoordinateRows(const QStringList &rows);
    /// 认 tp 命令那一格的结果：认出来了写回那三格与维度；没认出来就把原因摆出来。
    void setTeleportParse(bool ok, const QString &message);
    /// 「复制成 tp 命令」那一条：controller 拼好之后放进来，页面上那颗按钮直接拷它。
    void setTeleportCommand(const QString &command);

    // ================= 四、服务器在线检测 =================

    /// 要问的那个地址（`host:port`，没写端口按 25565 算 —— 那是 `ServerPing` 那边的事）。
    QString serverAddress() const;
    void setServerAddress(const QString &address);
    /// 结论。
    struct ServerView {
        bool ok = false;
        QString error;         ///< 非空 = 这次没问成（超时 / 没有应答 / 解析不了地址）
        bool timedOut = false; ///< 超时那一档（和"连上了但没答话"**分开**，见 ServerPing）
        /// 这一次的结论分类（`ServerPing::kindName()`；没问成时看它就知道是哪一类问题）。
        QString kind;
        QString address;       ///< 问的是哪个地址（回显一遍，免得用户看错行）
        QString addressKey;
        QString motd;          ///< 那一行彩色 MOTD 的 HTML（这一页把它当富文本画）
        QString version;       ///< 版本名
        QString players;       ///< `3 / 20`
        QString latency;       ///< `26 ms`
        QPixmap icon;          ///< 服务器图标（64×64；没有就是空的）
    };
    void setServerView(const ServerView &view);
    void clearServerResults();
    /// 「在线」那一格的字：`7 / 20`、`7`、或者**空串**（服务器没报在线人数）。
    ///
    /// **唯一出处** —— controller 摆 `ServerView::players` 和验证工程都看它。
    /// `online < 0` 是「服务器没说」，不是「0 个人」；把 -1 画到屏幕上等于报了个假数。
    static QString serverPlayerCountText(int online, int max);
    /// 正在问 / 问完了（按钮那两颗跟着换）。
    void setServerBusy(bool busy);

    // ================= 五、文件哈希校验 =================

    /// 算法那三档（名字由 controller 从 `FileHash::algorithmName()` 拿，页面上不写死）。
    void setAlgorithmChoices(const QStringList &names);
    int algorithmIndex() const;
    /// 要算的那个文件（拖进来 / 浏览选 / 手敲都在这一格）。
    QString hashPath() const;
    void setHashPath(const QString &path);
    /// 用户粘进来的"已知值"（可以为空 = 只想看摘要）。
    QString hashExpected() const;
    /// 算出来的结论。
    struct HashView {
        bool ok = false;
        QString error;        ///< 非空 = 没算成（打不开 / 取消了）
        QString hex;          ///< 摘要本身
        QString sizeText;     ///< 文件多大
        QString verdict;      ///< 和已知值对照的那句话
        bool hasVerdict = false;
        bool matches = false;
        QString algorithm;    ///< 算的是哪一档
    };
    void setHashView(const HashView &view);
    /// 批量算完之后那一列（文件名 / 大小 / 摘要；没算成的那几个带原因）。
    ///
    /// 和 `setHashView()` **是两档**：单文件那一份是"一个大摘要摆在正中间"，批量这一份是
    /// 一张清单 —— 两档各自把对方那份收掉（按钮、`hashBatchText()` 都跟着走），免得屏幕上
    /// 一半是上一次的单文件结果、一半是这一次的清单。
    struct HashBatchRow {
        QString name;      ///< 文件名（清单里那一行开头就是它）
        QString path;      ///< 完整路径（tooltip 与导出用）
        QString sizeText;  ///< 文件多大
        QString hex;       ///< 摘要（没算成时为空）
        bool ok = false;
        QString error;     ///< 没算成的原因
    };
    /// @param rows 已算完的那几个（顺序就是选文件的顺序）
    /// @param summary 顶上那句（"共 3 个文件 / SHA-256 / 1 个没算成"这种，controller 拼）
    void setHashBatchView(const QList<HashBatchRow> &rows, const QString &summary);
    /// 批量结果导出/复制的**纯文本**（`<摘要> <文件名>` 一行一个，标准校验和格式）。
    /// 没有批量结果时是空串。
    QString hashBatchText() const;
    /// 批量时那句"第 i / n 个：名字"（单文件那一趟不受影响）。
    void setHashProgressNote(const QString &text);
    /// 进度（`done` / `total`，一共那个是 -1 时写成"读了 X"）。
    void setHashProgress(qint64 done, qint64 total);
    void setHashBusy(bool busy);
    /// 算完之后那个文件被改过 / 不在了 ⇒ 摆这句提醒（空串 = 收起来）。
    ///
    /// 为什么这句必须露在结果旁边：屏幕上那串十六进制**看上去和刚才一模一样**，
    /// 但它已经不是这个文件现在的摘要了。不说的话，用户会拿它去和人家给的比，
    /// 然后得出一个反的结论（"明明重新下载过了还是对不上"）。
    void setHashStaleNote(const QString &text);

    // ================= 六、信标计算 =================

    /// 结果那一整段（`Beacon::detailText()` 拼好的；由 controller 摆进来）。
    void setBeaconResult(const QString &text);
    void setBeamResult(const QList<QColor> &segments, const QString &report);
    void setBeamColors(const QStringList &names);
    void setBeamLayers(const QList<int> &colors);
    void setBannerPreview(const QImage &image, const QString &notice);
    void setBannerPatternIcons(const QList<QIcon> &icons);
    /// 现在那两格问的是几级（「已有」0–4、「目标」1–4）。
    int beaconFromLevel() const;
    int beaconTargetLevel() const;
    /// 底座材质下标与颗数（材质次序由 controller 用 `Beacon::materialNames()` 摆进来）。
    void setBeaconMaterials(const QStringList &names);
    int beaconMaterialIndex() const;
    int beaconCount() const;
    /// 程序摆这两格（**不发 `beaconRequested`** —— 开页时给一个合理的默认）。
    void setBeaconLevels(int fromLevel, int targetLevel);

    // ================= 七、酿造助手 =================

    /// 药水下拉框的那几项（名字由 controller 从 `Brewing::potionNames()` 拿）。
    void setBrewingPotions(const QStringList &names);
    /// 形态下拉框的那几项（`普通` / `喷溅型` / `滞留型`）。
    void setBrewingForms(const QStringList &names);
    /// 目标版本下拉框的那几项（空串那一项显示成「不限版本」；程序摆值不发信号）。
    void setBrewingVersions(const QStringList &versions);
    /// 结果那一整段（`Brewing::Plan::detailText()` 拼好的）。
    void setBrewingResult(const QString &text);
    int brewingPotionIndex() const;
    bool brewingExtended() const;
    bool brewingLevel2() const;
    int brewingFormIndex() const;
    /// 要几瓶（1–64）。
    int brewingBottles() const;
    /// 目标版本（空串 = 不限版本）。
    QString brewingVersion() const;

    // ================= 八、旗帜设计 =================

    /// 底色 / 当前这一层的图案 / 当前这一层的颜色（三张表的次序都由 controller 给）。
    /// @p swatches 与 @p names 一一对应时，下拉框与图层清单的每个颜色名前画一颗色点。
    void setBannerColors(const QStringList &names, const QList<QColor> &swatches = {});
    void setBannerPatterns(const QStringList &names);
    /// 结果那一整段（`Banner::Plan::detailText()` 拼好的）。
    void setBannerResult(const QString &text);
    /// 「复制指令」那颗按的文本（`Banner::Plan::command`；空串 = 排不出来，那颗按不动）。
    void setBannerCommand(const QString &command);
    int bannerBaseColor() const;
    /// 已经排好的层，每层一项 `图案下标:颜色下标`（**顺序就是从下往上叠的顺序**）。
    QStringList bannerLayers() const;
    /// 程序摆层（**不发 `bannerRequested`**；验证工程与"恢复一份草稿"用）。
    void setBannerLayers(const QStringList &layers);
    void setBannerDesign(int base, const QStringList &layers, const QString &version);
    void setBannerVersions(const QStringList &versions);
    QString bannerVersion() const;
    void setBannerPatternAvailability(const QList<bool> &available);

    // ================= 九、纹饰搭配 =================

    void setTrimArmorMaterials(const QStringList &names);
    void setTrimParts(const QStringList &names);
    void setTrimPatterns(const QStringList &names);
    void setTrimMaterials(const QStringList &names);
    /// 目标版本下拉框的那几项（空串那一项显示成「不限版本」；程序摆值不发信号）。
    void setTrimVersions(const QStringList &versions);
    /// 目标版本（空串 = 不限版本）。
    QString trimVersion() const;
    /// 结果那一整段（controller 按四列合出来的材料账 + `ArmorTrim::plan()` 给的提示；
    /// 怎么算的不在这一层）。
    void setTrimResult(const QString &text);
    void setTrimPreview(const SkinModel::Scene &scene);
    void setTrimResourceStatus(const QString &text, bool busy = false);
    /// 当前账号有没有披风：没有就把「显示披风」那颗变灰并收回勾（不会自己画一条假的）。
    void setTrimCapeAvailable(bool available);
    /// 勾了「显示披风」吗（默认 false —— 披风是选修项，不是默认外观）。
    bool trimShowCape() const;
    struct TrimEquipmentSlot { int armor = -1; int pattern = -1; int material = 0; };
    QList<TrimEquipmentSlot> trimEquipment() const;
    void restoreTrimEquipment(bool perSlot);
    int trimArmorIndex() const;
    int trimPartIndex() const;
    int trimPatternIndex() const;
    int trimMaterialIndex() const;
    int trimPieces() const;
    /// 勾了「整套（四件）」吗（勾上时部位与件数两格收起来不用）。
    bool trimWholeSet() const;

signals:
    /// @p tool 那一片刚搭出来（每片只发一次，在 `toolShown()` 之前）。它那一组 set / get 从这一刻起能用。
    void toolBuilt(ToolboxPage::Tool tool);
    /// 进了哪一件工具（目录格、切换浮层、程序里调 `showTool()` 都在这里发一次）。
    void toolShown(ToolboxPage::Tool tool);
    /// 回到了目录页。
    void catalogShown();
    void trimEquipmentChanged();
    /// 「显示披风」被勾上 / 取消（披风图由 controller 给，这一层只报这个开关）。
    void trimCapeVisibilityChanged(bool visible);
    void trimResourcesRequested(int action); // 0: scan local, 1: choose archive, 2: download, 3: cancel
    void beamRequested(const QList<int> &glass);
    void bannerTexturesRequested();
    void bannerSaveRequested();
    void bannerLoadRequested();
    // ---- 附魔顺序助手 ----
    void anvilPlanRequested();
    // ---- 文本生成器 ----
    void textSourceChanged(const QString &text);
    void textPreviewEdited();
    void textJsonRequested(const QString &json);  ///< 用户点了「从 JSON 读回来」
    /// 用户在自定颜色那一格敲了 `#RRGGBB`、点了「插进去」：controller 验一下、算成
    /// `§x…` 再调 `insertTextAtCursor()`。
    void textHexColorRequested(const QString &hex);
    // ---- 坐标换算 ----
    void coordinateRequested(int dimensionIndex, double x, double y, double z);
    void teleportParseRequested(const QString &text);
    void teleportCommandRequested(int dimensionIndex, double x, double y, double z);
    // ---- 服务器在线检测 ----
    void serverCheckRequested(const QString &address);
    void serverCheckCancelRequested();
    void serverToolExited();
    // ---- 文件哈希校验 ----
    void hashRequested(const QString &path, int algorithmIndex, const QString &expected);
    void hashCancelRequested();
    /// 批量算（「补多文件」）。`paths` 是用户一次选中的那几个。
    void hashBatchRequested(const QStringList &paths, int algorithmIndex);
    /// 把批量结果的纯文本写到 @p path（写盘那一半在 controller；这一层只给路和字）。
    void hashExportRequested(const QString &path, const QString &text);
    // ---- 信标计算 / 酿造助手 ----
    void beaconRequested(int fromLevel, int targetLevel, int materialIndex, int beaconCount);
    void brewingRequested(int potionIndex, bool extended, bool level2, int formIndex,
                          int bottles);
    // ---- 旗帜设计 / 纹饰搭配 ----
    // / @p layers 每层一项 `图案下标:颜色下标`，顺序 = 从下往上叠的顺序。
    void bannerRequested(int baseColor, const QStringList &layers);
    void trimRequested(int armorMaterial, int part, int patternIndex, int trimMaterial,
                       int pieces, bool wholeSet);

protected:
    void hideEvent(QHideEvent *event) override;
    /// 拖一个文件进来（只对「SHA 校验」这一片有意义 —— 别的工具不吃文件）。
    /// 判据见 .cpp：当前不是哈希那一片、或者拖进来的不是本地文件，就原样交给基类。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 搭 @p tool 那一片（已搭过就直接返回），返回 `m_stack` 里那一页。
    QWidget *ensureTool(Tool tool);
    /// 十片各自搭一次（`ensureTool()` 调，之后切的时候只是换 `QStackedWidget` 那一格）。
    QWidget *buildAnvilPanel();
    QWidget *buildTextPanel();
    QWidget *buildCoordPanel();
    QWidget *buildServerPanel();
    QWidget *buildHashPanel();
    QWidget *buildBeaconPanel();
    QWidget *buildBrewingPanel();
    QWidget *buildBannerPanel();
    QWidget *buildTrimPanel();
    QWidget *buildDownloadPanel();
    /// 页头（目录页：标题 + 记住输入；工具页：面包屑）。
    QWidget *buildHeader();
    QWidget *buildCatalog();
    /// 弹出切换浮层（面包屑上的工具名）。
    void openSwitchPopover();
    void syncHeader();
    /// 附魔那一张表加一行（「加一行」和"构造时先摆一行"走的是同一段）。
    void appendAnvilRow();
    /// 删掉 / 加上一行之后重新摆一遍：第一行的「同上一件」去掉、悬停删除钮、窄时折行。
    void refreshAnvilRows();
    /// 哈希结果区右上那组钮：单文件只留「复制」，批量清单才出「复制全部 / 导出…」。
    void syncHashActions();
    /// 旗帜那一排层的列表按 `m_bannerLayers` 重画一遍（行名从两张下拉框里取）。
    void syncBannerList();
    /// 旗帜这一份改了（底色 / 层序）之后发一发。
    void emitBannerNow();

    Tool m_current = Tool::Anvil;
    QList<Tool> m_tools;
    bool m_inCatalog = true;
    QStackedWidget *m_levels = nullptr;  ///< 0 = 目录页，1 = 工具页
    QStackedWidget *m_stack = nullptr;   ///< 已搭出来的那几片，按第一次进的先后
    QMap<Tool, QWidget *> m_pages;       ///< 已搭的工具 → `m_stack` 里那一页
    QWidget *m_headerInner = nullptr;
    QWidget *m_catalogHead = nullptr;
    QWidget *m_crumbHead = nullptr;
    QPushButton *m_crumbRoot = nullptr;
    QPushButton *m_crumbTool = nullptr;
    QCheckBox *m_remember = nullptr;
    QWidget *m_catalogBody = nullptr;
    DownloadToolPanel *m_download = nullptr;

    // 一、附魔顺序助手
    QWidget *m_anvilRowsHost = nullptr;      ///< 一行一行的输入区（固定列宽网格）
    QList<QWidget *> m_anvilRowWidgets;      ///< 每行一个容器；容器 property "role" 区分框
    QCheckBox *m_anvilRename = nullptr;
    QWidget *m_anvilResultBox = nullptr;
    QLabel *m_anvilStepCount = nullptr;
    QList<QPair<QString, QString>> m_anvilChoices;
    QLabel *m_anvilResult = nullptr;
    QLabel *m_anvilError = nullptr;

    // 二、文本生成器
    QPlainTextEdit *m_textSource = nullptr;
    QTextEdit *m_textPreview = nullptr;
    QPlainTextEdit *m_textJson = nullptr;
    QPlainTextEdit *m_textMotd = nullptr;
    QLineEdit *m_textHex = nullptr;
    QLabel *m_textNotice = nullptr;
    QGridLayout *m_textPaletteLayout = nullptr;
    QWidget *m_textPaletteRow = nullptr;

    // 三、坐标换算
    QComboBox *m_coordDimension = nullptr;
    QDoubleSpinBox *m_coordX = nullptr;
    QDoubleSpinBox *m_coordY = nullptr;
    QDoubleSpinBox *m_coordZ = nullptr;
    QLineEdit *m_tpCommand = nullptr;
    QLabel *m_tpNotice = nullptr;
    QLabel *m_coordResult = nullptr;

    // 四、服务器在线检测
    QLineEdit *m_serverInput = nullptr;
    QPushButton *m_serverCheck = nullptr;
    QPushButton *m_serverCancel = nullptr;
    QLabel *m_serverIcon = nullptr;
    QLabel *m_serverResult = nullptr;
    QListWidget *m_serverHistory = nullptr;

    // 五、文件哈希校验
    QLineEdit *m_hashPath = nullptr;
    QComboBox *m_hashAlgorithm = nullptr;
    QLineEdit *m_hashExpected = nullptr;
    QPushButton *m_hashRun = nullptr;
    QPushButton *m_hashCancel = nullptr;
    QLabel *m_hashResult = nullptr;
    QLabel *m_hashProgress = nullptr;
    /// 算完之后文件变过的那句提醒（见 `setHashStaleNote()`）。
    QLabel *m_hashNotice = nullptr;
    /// 算出来的那一串摘要（「复制」那颗拷的是它 —— 结果标签里画的带 HTML，不能整段拷）。
    QString m_hashHex;
    /// 批量那一档（「补多文件」+「复制/导出列表」）。
    QPushButton *m_hashCopyAll = nullptr;  ///< 「复制全部」（标准校验和文本）
    QPushButton *m_hashExport = nullptr;   ///< 「导出…」（写一个 .txt）
    /// 批量结果的纯文本（`<摘要> <文件名>` 一行一个）。空串 = 没有批量结果。
    QString m_hashBatchText;

    // 六、信标计算
    QSpinBox *m_beaconFrom = nullptr;
    QSpinBox *m_beaconLevel = nullptr;
    QComboBox *m_beaconMaterial = nullptr;
    QSpinBox *m_beaconCount = nullptr;
    QLabel *m_beaconResult = nullptr;

    // 七、酿造助手
    QComboBox *m_brewingPotion = nullptr;
    QCheckBox *m_brewingExtended = nullptr;
    QCheckBox *m_brewingLevel2 = nullptr;
    QComboBox *m_brewingForm = nullptr;
    QSpinBox *m_brewingBottles = nullptr;
    QLabel *m_brewingResult = nullptr;

    // 八、旗帜设计
    QComboBox *m_bannerBase = nullptr;
    QComboBox *m_bannerPattern = nullptr;
    QComboBox *m_bannerColor = nullptr;
    QPushButton *m_bannerAdd = nullptr;
    QListWidget *m_bannerList = nullptr;
    QPushButton *m_bannerUp = nullptr;
    QPushButton *m_bannerDown = nullptr;
    QPushButton *m_bannerRemove = nullptr;
    QPushButton *m_bannerUndo = nullptr;
    QPushButton *m_bannerCopy = nullptr;
    QLabel *m_bannerResult = nullptr;
    /// 已经排好的层（`图案下标:颜色下标`），顺序 = 从下往上叠。
    QStringList m_bannerLayers;
    QList<QColor> m_bannerSwatches;
    /// 撤销栈（每一次改动前先把上一份抄进来；**只留最近二十次**，免得一直长）。
    QList<QStringList> m_bannerUndoStack;
    /// 「复制指令」那颗按的：controller 递进来的那条 `/give`（排不出来时是空串 ⇒ 收起来）。
    QString m_bannerCommand;
    /// 程序拽列表行时挡一下（`currentRowChanged` 只影响按钮可用性，不影响层）。
    bool m_bannerSyncing = false;

    // 九、纹饰搭配
    QComboBox *m_trimArmor = nullptr;
    QComboBox *m_trimPart = nullptr;
    QComboBox *m_trimPattern = nullptr;
    QComboBox *m_trimMaterial = nullptr;
    QSpinBox *m_trimPieces = nullptr;
    QCheckBox *m_trimWholeSet = nullptr;
    QLabel *m_trimResult = nullptr;
    ArmorTrimPreview *m_trimPreview = nullptr;
    QCheckBox *m_trimCape = nullptr;
    QLabel *m_trimResourceStatus = nullptr;
    QList<QPushButton *> m_trimResourceButtons;
    QList<QComboBox *> m_trimSlotArmor, m_trimSlotPattern, m_trimSlotMaterial;
    bool m_trimSyncing = false;
};
