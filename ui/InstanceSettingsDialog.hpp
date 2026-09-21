/**
 * @file InstanceSettingsDialog.hpp
 * @brief 「实例设置…」对话框：给单个实例覆盖启动五项（Java 路径 / Xmx / Xms / 内存自动 /
 *        额外 JVM 参数），每项都是两级「跟随全局 / 自定义」。
 * @author 0x7fffff
 * @date 2026-09-21
 * @note 进参出参都是**值**，本文件不 include 任何 src/ 下的头 —— 和 GroupDialog 同一条
 *       约定（ui/ 不依赖业务层）。所以这里的 Override 是它自己的一份，和
 *       src/settings/EffectiveJavaSettings.hpp 里的 JavaSettingsOverride 逐字段相同，
 *       由 MainWindow 在两边转换。
 *
 * 【哪五项能覆盖，别的为什么不能】
 *
 * 只做**启动五项**。其余设置项各有各的归属，混进来只会让"这个实例"变成一个说不清的概念：
 *   - 下载源（Download/Source）：下载那一刻的决定，和"启动这个实例"无关；
 *   - 启动后（Launcher/AfterLaunch）：那是启动器自己的行为，不是这个实例的属性；
 *   - 版本名格式那四个开关：读侧 nameFormat 早就支持，本轮不做界面。
 *
 * 【约定：对话框只改自己手里那份值，不碰磁盘】
 *
 * 用户点「确定」之前，wtfml.meta 一个字节都不动 —— 和其它对话框一样。这里连"确定"
 * 都不写盘：它只把控件读成一份值（override() / appendJvmArgs()），落盘在
 * MainWindow::applyInstanceSettings() 里。
 *
 * 【约定：控件的值就是结果（没有内部影子状态）】
 *
 * override() **现场读控件**，不缓存一份 m_override。这样"摆好控件 → 读结果"在验证
 * 工程里可以被直接驱动，而不用先走一遍 exec()（模态窗在 offscreen 下驱动不了）。
 */
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QToolButton;
class QWidget;

class InstanceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    /// 这个实例覆盖的那五项。没有值 = 跟随全局（空串也算有值，见 EffectiveJavaSettings.hpp）。
    struct Override {
        std::optional<QString> javaPath;
        std::optional<int> xmx;
        std::optional<int> xms;
        std::optional<bool> autoMemory;
        std::optional<QString> extraJvmArgs;
    };

    /// 全局那一层的值。只用来写「跟随全局（当前 …）」那句话 —— 用户要能看见自己
    /// 跟随的到底是什么数，否则"跟随全局"就是一格写不出内容的空白。
    struct GlobalValues {
        QString javaPath;
        int xmx = 0;
        int xms = 0;
        bool autoMemory = true;
        QString extraJvmArgs;
    };

    /**
     * @brief 构造对话框。
     * @param instanceName 显示在标题里的实例名（用户认得出改的是哪一个）。
     * @param current 这个实例**现在**的覆盖（wtfml.meta 里读出来的）。
     * @param global 全局有效值（GlobalJavaSettings::effective()），用来写「当前 …」。
     * @param appendJvmArgs 这个实例现有的「追加参数」（wtfml.meta 的 jvmArgs 数组）。
     *        它不是覆盖层的一项，和全局没有"跟随"关系，所以单独一行、没有那对单选钮。
     * @param parent 父窗口。
     */
    InstanceSettingsDialog(const QString &instanceName, const Override &current,
                           const GlobalValues &global, const QStringList &appendJvmArgs,
                           QWidget *parent = nullptr);

    /// 用户摆好的覆盖。五项各自：那行选了「自定义」就有值，选了「跟随全局」就没有值。
    ///
    /// NOTE: 「内存分配」若是自动档（这一行选「自定义」且勾着自动，或者跟随全局而全局
    ///       是自动），最大/最小内存**一律当成跟随全局**（那两行这时是禁用的）。
    ///       自动档下这两个数由系统内存现算，存一份永远不会被读的覆盖就是骗人 ——
    ///       "让用户以为能改"正是设置页那条注释明确反对的事。
    Override override() const;

    /// 高级选项里那两行之一：**追加**在命令行最后的 JVM 参数（写进 meta 的 jvmArgs，
    /// 是数组不是空格分隔的串 —— 参数本身可能含空格）。
    QStringList appendJvmArgs() const;

    /// 高级选项那一块现在是不是展开着。默认折叠。
    bool advancedExpanded() const;

    /// 自动内存是不是**有效**开着（这一行自定义了就听它的，否则听全局的）。
    /// 界面用它决定下面两个数字框禁不禁用；验证工程用它当"这个实例这次是自动还是手动"
    /// 的判据。
    bool effectiveAutoMemory() const;

    /// 刷新每一行下面那句话，以及两行内存的可用性（自动档下整行禁用）。
    /// 每次那对单选钮或自动勾选框变化都会调它。
    void refreshHints();

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件，别改回按文字去认（文字会随全局值变）。
    /// 某一项的「跟随全局」/「自定义」单选钮。@param field 见下面那五个常量。
    static QString followRadioName(const QString &field) {
        return QStringLiteral("override_%1_follow").arg(field);
    }
    static QString customRadioName(const QString &field) {
        return QStringLiteral("override_%1_custom").arg(field);
    }
    /// 某一行「跟随全局（当前 …）」那句话的 objectName。
    static QString followHintName(const QString &field) {
        return QStringLiteral("override_%1_hint").arg(field);
    }
    /// 某一行「档位那一行」的 objectName —— 里面是 [跟随全局][自定义] + 那一项的控件。
    /// 那一项的控件和它的档位是**同一个决定的两半**（选「自定义」+ 自定义成什么），
    /// 所以并排在同一行；这一整行下面才是那一句解释（followHintName）。
    static QString choiceRowName(const QString &field) {
        return QStringLiteral("override_%1_choice").arg(field);
    }
    static QString javaPathField() { return QStringLiteral("javaPath"); }
    static QString xmxField() { return QStringLiteral("xmx"); }
    static QString xmsField() { return QStringLiteral("xms"); }
    static QString autoMemoryField() { return QStringLiteral("autoMemory"); }
    static QString extraJvmArgsField() { return QStringLiteral("extraJvmArgs"); }

    static QString javaPathEditName() { return QStringLiteral("instanceJavaPathEdit"); }
    static QString browseJavaButtonName() {
        return QStringLiteral("instanceBrowseJavaButton");
    }
    static QString memoryMaxSpinName() { return QStringLiteral("instanceMemoryMaxSpin"); }
    static QString memoryMinSpinName() { return QStringLiteral("instanceMemoryMinSpin"); }
    static QString memoryAutoCheckName() { return QStringLiteral("instanceMemoryAutoCheck"); }
    static QString extraJvmArgsEditName() {
        return QStringLiteral("instanceExtraJvmArgsEdit");
    }
    static QString appendJvmArgsEditName() {
        return QStringLiteral("instanceAppendJvmArgsEdit");
    }
    static QString advancedToggleName() { return QStringLiteral("instanceAdvancedToggle"); }
    static QString advancedBoxName() { return QStringLiteral("instanceAdvancedBox"); }
    static QString titleLabelName() { return QStringLiteral("instanceSettingsTitle"); }
    static QString confirmButtonName() {
        return QStringLiteral("instanceSettingsConfirmButton");
    }
    static QString cancelButtonName() { return QStringLiteral("instanceSettingsCancelButton"); }

    /// 那一对单选钮的文字。**这两句是产品文案**，验证工程按它们核对而不是自己拼。
    static QString followText() { return QStringLiteral("跟随全局"); }
    static QString customText() { return QStringLiteral("自定义"); }
    /// 「跟随全局（当前 %1）」。@param current 全局那一项的值的可读写法。
    static QString followHint(const QString &current) {
        return QStringLiteral("跟随全局（当前 %1）").arg(current);
    }
    /// 自动档下两行内存数字框的提示（也是它们被禁用的原因）。
    static QString memoryAutoDisabledHint() {
        return QStringLiteral("「内存分配」是自动档：这两个数按系统内存现算，覆盖不生效。");
    }
    /// 高级选项那个折叠按钮上的字。默认折叠 —— 里面两项都是"改错了会让游戏起不来"
    /// 的东西，不该一打开对话框就摆在眼前。
    static QString advancedToggleText() { return QStringLiteral("高级选项"); }

    /// 追加参数那行提示的正文（也是"一条一行"这条约定的唯一出处）。
    static QString appendJvmArgsHint() {
        return QStringLiteral("一条一行。排在全局额外参数**之后**（同一条以后写的为准），"
                              "不覆盖全局那一份。");
    }

private:
    /// 造一行「跟随全局 / 自定义 + 控件」。返回值是**整格**（档位行 + 提示），控件要加进
    /// 里面的「档位那一行」（`choiceRowOf()` 取），内存那两行整格禁用靠的就是这个返回值。
    QWidget *addOverrideRow(class QGridLayout *grid, int row, const QString &field,
                            const QString &title, const QString &hint);

    /// 取某一行装控件的那条横排（`choiceRowName()`）。
    ///
    /// NOTE: 控件加进这里而不是加进整格，是因为整格的布局是**竖排**（档位行 / 提示）。
    ///       写成 `qobject_cast<QHBoxLayout *>(cell->layout())` 会拿到 nullptr，
    ///       再往下就是空指针解引用 —— 这张表里每一行都写着那一句，所以收口成一个函数。
    QHBoxLayout *choiceRowOf(const QString &field) const;

    /// 全局那一层的值，用来写「跟随全局（当前 …）」。构造之后不再变。
    GlobalValues m_global;

    QLineEdit *m_javaPathEdit = nullptr;
    QSpinBox *m_memoryMaxSpin = nullptr;
    QSpinBox *m_memoryMinSpin = nullptr;
    QCheckBox *m_memoryAutoCheck = nullptr;
    QLineEdit *m_extraJvmArgsEdit = nullptr;
    /// 追加参数那一行是**多行**输入（一条一行）而不是单行：meta 里的 jvmArgs 是数组，
    /// 数组存在的理由就是"参数本身可能含空格"（见 VersionManager::versionJvmArgs()）。
    /// 用单行输入就得在"读出来拼成一行"和"存回去切成数组"之间来回切，而那个来回是
    /// **有损**的：`-Dfoo=bar baz` 会被切成两条。一条一行则原样进原样出。
    QPlainTextEdit *m_appendJvmArgsEdit = nullptr;
    QToolButton *m_advancedToggle = nullptr;
    QWidget *m_advancedBox = nullptr;

    // 五行各一对单选钮。存在成员里（而不是每次 findChild）是因为 refreshHints()
    // 每次信号都要读它们算"有效值"，而那是按键最频繁的一条路。
    QRadioButton *m_javaPathFollow = nullptr;
    QRadioButton *m_javaPathCustom = nullptr;
    QRadioButton *m_xmxFollow = nullptr;
    QRadioButton *m_xmxCustom = nullptr;
    QRadioButton *m_xmsFollow = nullptr;
    QRadioButton *m_xmsCustom = nullptr;
    QRadioButton *m_autoMemoryFollow = nullptr;
    QRadioButton *m_autoMemoryCustom = nullptr;
    QRadioButton *m_extraJvmArgsFollow = nullptr;
    QRadioButton *m_extraJvmArgsCustom = nullptr;
    /// 「最大内存」「最小内存」两行左边那两对单选钮的容器 —— 自动档下整行禁用，
    /// 禁用的是这两个容器（连同里面那对钮），不是只禁数字框。
    QWidget *m_xmxFollowCell = nullptr;
    QWidget *m_xmsFollowCell = nullptr;
    /// 每一项在「自定义」状态下那句话（选「跟随全局」时说的是"当前全局是哪个值"，
    /// 两种形态都由 refreshHints() 写进同一个 QLabel）。
    QHash<QString, QString> m_customHints;
};
