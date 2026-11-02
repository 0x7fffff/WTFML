/**
 * @file ToolboxPage.hpp
 * @brief 工具箱那一整页（底栏那把扳手 → 一片整块盖住内容区的页），五件小工具共用一页；
 * 第六件「高速下载」不在这一页里，它开自己的独立窗口（见下面的【第六件】）。
 * @note 这一页收五件小工具：文件哈希校验、MOTD 文本生成器（含彩色文本那几种
 * 命令写法）、坐标换算（认 F3+C 复制来的那行 tp 命令）、服务器在线检测，外加
 * 「基于 mc 的铁砧惩罚机制」的附魔顺序助手。
 *
 * 【为什么五件工具在一个页里，而不是五片页】
 *
 * 这一页和「设置」那页是一个形状：**顶上五颗 tool chip，底下那五件工具各自一片、换着露**
 * （`QStackedWidget`）。理由也一样：这五件事互相之间没有次序、没有前后依赖，用户要的是
 * "随手拿一件用一下就走"，为它各开一页只会让底栏那把扳手变成五个入口。chip 那一排的写法
 * 照抄下载页四颗 chip / 文件页四颗筛选按钮（同一套 `Theme::markButton` 和选中判据）。
 *
 * 【第六件：高速下载为什么不在 chip 里】
 *
 * 直链下载那一件**和这五件不是一个形状**：它要一直挂着一个正在跑的任务
 * （进度、取消、关掉窗口还得接着下），塞进"随手用一下就走"的 chip 页会把这一页变成半个
 * 任务中心。所以它仍然登记在**同一张表** `kToolInfos` / `tools()` 里（键、名字、说明、底栏
 * 面板那颗按钮都跟着这张表走），但 `m_tools` 在构造里把它摘掉：chip 只有五颗、
 * `QStackedWidget` 也只有五片，它由 `ToolboxController::open("download")` 转给独立窗口。
 * 工具箱里的工具允许开独立小窗口（以后可能加到十条）。
 *
 * 【这一页自己一根手指都不碰游戏目录】
 *
 * `ui/` **不 include `src/`**（全仓的规矩）。所以这一页只干两件事：把用户敲的东西
 * **收成信号**发出去、把 `ToolboxController` 算好的**字符串**摆上去。算哈希、认坐标、
 * 拼 `tellraw`、排附魔顺序那些全在 `src/tools/` 里（纯函数，各自有判据在 tests/main.cpp）。
 *
 * 【"算"这件事为什么不在这一页里做】
 *
 * 哈希要读一个可能几百 MB 的文件（要进度、要能取消），服务器检测要联网（要超时），
 * 这两件都是**慢活**：它们必须由 controller 派到线程上去，页面上那颗按钮只负责发一句
 * "用户要算了"。另三件是纯字符串活，本来当场就能算，但为了"这几件工具一个形状"，入口
 * 一律是同一条（发信号 → controller 算 → 回来填页面）。「高速下载」不走这一条：它整条链
 * 在 `DirectDownloadController` 里，窗口只管收输入和摆状态（见头文件【第六件】）。
 */

#pragma once

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QPixmap>

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
class QStackedWidget;
class QTableWidget;
class QWidget;
class ArmorTrimPreview;
namespace SkinModel { struct Scene; }

class ToolboxPage : public QDialog {
    Q_OBJECT

public:
    /// 十件工具。**`tools()` 是 chip 那一排的次序唯一出处**：顶上那排 chip 用其中除
    /// `Download` 之外的九件（`.cpp` 构造里摘的）；底栏面板另按 `panelTools()` 摆
    /// （两列五排）。
    enum class Tool { Anvil, Text, Coord, Server, Hash, Beacon, Brewing, Banner, Trim, Download };

    /// 工具的键（底栏面板那六颗按钮、`ToolboxController::open()` 都用它认人）。
    static QString toolKey(Tool tool);
    /// 反着来。认不出来时 @p ok 写 false。
    static Tool toolFromKey(const QString &key, bool *ok = nullptr);
    /// 中文名（`附魔顺序助手`）。
    static QString toolName(Tool tool);
    /// 一句话说明（chip 的 tooltip、以及每一片顶上那行小字）。
    static QString toolSummary(Tool tool);
    /// 八件，**次序唯一出处**（`Download` 排第一；它不出 chip、只出底栏面板那颗按钮）。
    static QList<Tool> tools();
    /// 底栏那块「工具箱」面板的摆放次序（**两列五排、
    /// 行优先**）。它和 `tools()` 故意不是同一个次序 —— `tools()` 是 chip 那一排的次序，
    /// 面板按常用程度从左到右、从上到下另排一张。返回的十件一件不多一件不少。
    static QList<Tool> panelTools();
    /// 面板上那一行短名（`SHA校验` / `延迟测试` …）。**页面 / chip 仍用 `toolName()`**
    /// （那是完整名字，写在每一片顶上）；面板窄，只放得下两三个字的那个。
    static QString toolShortName(Tool tool);
    /// 面板上每一件工具左边画的那枚 24×24 线稿图标。**不是位图资源** —— 现画，
    /// 颜色由调用方给（面板拿主题色那一档）。
    static QIcon toolIcon(Tool tool, const QColor &ink);
    /// 面板标题旁那把扳手（同一个画法）。
    static QIcon panelTitleIcon(const QColor &ink);    /// 这一页的标题（`OverlayHost::showPage` 顶上那一条写的字）。
    static QString windowTitle() { return QStringLiteral("工具箱"); }

    /// 验证工程认控件用的那几个名字（`@p tool` 那一档的 chip / 页 / 以及**底栏面板上
    /// 对应的那颗按钮** —— 后者长在 `MainWindowUI` 里，但名字跟着这张表走）。
    static QString toolChipName(Tool tool);
    static QString toolPanelName(Tool tool);
    /// 面板外面那层滚动容器 —— **它才是 `QStackedWidget` 的那一页**。换工具时显隐落在
    /// 这一层，面板自己是它的子控件（`panel->isHidden()` 量不出"这一片收起来了"）。
    static QString toolPageName(Tool tool);
    static QString toolButtonName(Tool tool);

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
    static QString hashBatchName() { return QStringLiteral("toolboxHashBatch"); }
    static QString hashCopyAllName() { return QStringLiteral("toolboxHashCopyAll"); }
    static QString hashExportName() { return QStringLiteral("toolboxHashExport"); }

    explicit ToolboxPage(QWidget *parent = nullptr);

    /// 切到 @p tool（开页时也走它）。
    void showTool(Tool tool);
    Tool currentTool() const { return m_current; }

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
    /// 三种输出：`§` 写法、JSON 组件、`/tellraw` 命令。
    void setTextOutputs(const QString &legacy, const QString &json, const QString &tellraw);
    /// 第四种：`server.properties` 那一整行 `motd=…`（controller 已经把非 ASCII 转成
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
        QString motd;          ///< 那一行彩色 MOTD 的 HTML（这一页把它当富文本画）
        QString version;       ///< 版本名
        QString players;       ///< `3 / 20`
        QString latency;       ///< `26 ms`
        QPixmap icon;          ///< 服务器图标（64×64；没有就是空的）
    };
    void setServerView(const ServerView &view);
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
    /// @param summary 顶上那句（"共 3 个文件 · SHA-256 · 1 个没算成"这种，controller 拼）
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
    void setBannerColors(const QStringList &names);
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
    /// 换到哪一件工具（顶上那排 chip 和工具箱面板都走 `showTool()`，只在这里发一次）。
    void toolShown(ToolboxPage::Tool tool);
    void trimEquipmentChanged();
    /// 「显示披风」被勾上 / 取消（披风图由 controller 给，这一层只报这个开关）。
    void trimCapeVisibilityChanged(bool visible);
    void trimResourcesRequested(int action); // 0: scan local, 1: choose archive, 2: download, 3: cancel
    void beamRequested(const QList<int> &glass);
    void bannerTexturesRequested();
    void bannerSaveRequested();
    void bannerLoadRequested();
    void downloadToolRequested();
    // ---- 附魔顺序助手 ----
    void anvilPlanRequested();
    // ---- 文本生成器 ----
    void textSourceChanged(const QString &text);
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
    /// 拖一个文件进来（只对"哈希校验"这一件有意义 —— 别的工具不吃文件）。
    /// 判据见 .cpp：当前不是哈希那一件、或者拖进来的不是本地文件，就原样交给基类。
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 五片各自搭一次（构造函数里全搭好，切的时候只是换 `QStackedWidget` 那一格）。
    /// NOTE: `Download` 不占 `QStackedWidget` 的一格（它不是这一页的一片，见头文件）。
    QWidget *buildAnvilPanel();
    QWidget *buildTextPanel();
    QWidget *buildCoordPanel();
    QWidget *buildServerPanel();
    QWidget *buildHashPanel();
    QWidget *buildBeaconPanel();
    QWidget *buildBrewingPanel();
    QWidget *buildBannerPanel();
    QWidget *buildTrimPanel();
    /// 顶上那排 chip（点一下 = showTool）。
    QWidget *buildChipRow();
    /// 附魔那一张表加一行（「加一行」和"构造时先摆一行"走的是同一段）。
    void appendAnvilRow();
    /// 删掉一行之后把「和上一行同一件」那一列重新摆一遍（第一行没有上一行）。
    void refreshAnvilSameColumn();
    /// chip 的选中态（只认当前那一颗，和下载页四颗 chip 同一套判据）。
    void syncChips();
    /// 旗帜那一排层的列表按 `m_bannerLayers` 重画一遍（行名从两张下拉框里取）。
    void syncBannerList();
    /// 旗帜这一份改了（底色 / 层序）之后发一发。
    void emitBannerNow();

    Tool m_current = Tool::Anvil;
    QList<Tool> m_tools;
    QStackedWidget *m_stack = nullptr;
    QList<QPushButton *> m_chips;

    // 一、附魔顺序助手
    QTableWidget *m_anvilTable = nullptr;
    QCheckBox *m_anvilRename = nullptr;
    QList<QPair<QString, QString>> m_anvilChoices;
    QLabel *m_anvilResult = nullptr;
    QLabel *m_anvilError = nullptr;

    // 二、文本生成器
    QPlainTextEdit *m_textSource = nullptr;
    QLabel *m_textPreview = nullptr;
    QPlainTextEdit *m_textLegacy = nullptr;
    QPlainTextEdit *m_textJson = nullptr;
    QPlainTextEdit *m_textTellraw = nullptr;
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
    QLineEdit *m_tpInput = nullptr;
    QLineEdit *m_tpCommand = nullptr;
    QLabel *m_tpNotice = nullptr;
    QLabel *m_coordResult = nullptr;

    // 四、服务器在线检测
    QLineEdit *m_serverInput = nullptr;
    QPushButton *m_serverCheck = nullptr;
    QPushButton *m_serverCancel = nullptr;
    QLabel *m_serverIcon = nullptr;
    QLabel *m_serverResult = nullptr;

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
    QPushButton *m_hashBatch = nullptr;    ///< 「批量…」——选好几个文件一次算
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
