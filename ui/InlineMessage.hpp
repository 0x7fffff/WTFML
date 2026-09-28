/**
 * @file InlineMessage.hpp
 * @brief `QMessageBox` 的替身：提示开在**主窗口内容区中间**（四周变暗），不再另开一个窗。
 * @author 0x7fffff
 * @date 2026-09-25
 *
 * 【为什么要有这一层】
 * 全仓原来有 90 来处 `QMessageBox`（`src/MainWindow.cpp` 86 处 + `ui/GroupDialog.cpp` 4 处）。
 * 用户给的方向是"最终不出现独立窗口"，而"说一句""问一句"正是最碎、最常出现的那一类。
 * 所以这里给出一组**和 QMessageBox 同名同义**的封装：
 *
 *     QMessageBox::warning(this, t, x);   →   InlineMessage::warning(m_ui->overlay(), t, x);
 *
 * 换过去时**语义一个字不变**（同步返回、按钮含义、默认按钮），于是九十来处可以一处一处
 * 地换、每次都能单独编过、单独验，而不是先炸掉一大片。
 *
 * 【尺寸只有一套】
 * 面板宽度 = `OverlayHost::promptContentWidth()`（唯一出处）。用户点名要的
 * 「不会让界面出现尺寸不一样的弹窗」就是这一条：所有提示一样宽，只有高矮随字数走。
 *
 * NOTE: v1 **不画图标**。QMessageBox 那个感叹号/问号这里没有替代品 —— 标题已经把那句话
 *       的性质说清楚了（「下载失败」「删除账号」），加一个自己画的图标反而是新开销。
 *       要加的话加在这里，别在调用点各画各的。
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QLineEdit;
class QPushButton;
class OverlayHost;

/// 这一句话是哪一类。只影响**标题的颜色**（见 .cpp 里那段 NOTE）。
enum class MessageKind {
    Information,
    Warning,
    Critical,
    Question,
};

/**
 * @brief 内容区中间那一块提示面板。
 *
 * 它是 `QDialog`（而不是普通 `QWidget`）：这样 Esc = 拒绝、回车 = 默认按钮、`finished()`
 * 这些"一问一答"该有的东西全是白拿的，而且能直接塞进 `OverlayHost::execPrompt()`。
 * **它自己从来不是窗口** —— 覆盖层会先把 `Qt::Dialog` 摘掉再摆上去。
 */
class InlineMessageBox : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief 构造。
     * @param kind 哪一类（只影响标题颜色）。
     * @param title 标题（粗体那一行）。
     * @param text 正文。**可以多段**（里面有 \n\n 就是分段），宽度是固定的，
     *        所以换行由它自己撑开 —— 调用点不用管排版。
     * @param buttons 按钮文字，从左到右。空的话补一颗「确定」。
     * @param defaultIndex 默认按钮的下标（回车打的那一颗）。**-1 = 没有默认按钮** ——
     *        "别让一次误按回车就把事办了"那种场合用它（比如「停止游戏」）。
     * @param destructiveIndex 危险动作那一颗（停止游戏 / 删除）—— 只影响配色。**-1 = 没有**。
     * @param parent 父控件（覆盖层会重新认领它，这里给不给都不影响它是子控件）。
     */
    InlineMessageBox(MessageKind kind, const QString &title, const QString &text,
                     const QStringList &buttons, int defaultIndex, int destructiveIndex,
                     QWidget *parent = nullptr);

    /// 被点中的按钮下标。**关掉 / 按 Esc / 点外面都停在 -1**（= 什么都没选）。
    int clickedIndex() const { return m_clicked; }

    /// 这一句是哪一类。**给验证工程用的**：以前那条路量的是
    /// `QMessageBox::icon() == QMessageBox::Warning`（"这是出错还是一句报信"），
    /// 换过来之后对应的就是这一格 —— 只占一行的公开访问器，比让测试去猜标题的颜色结实。
    MessageKind kind() const { return m_kind; }

    /**
     * @brief 在**正文和按钮行之间**插一行输入框，返回它。
     *
     * 这是 `QInputDialog::getText` 那一档的替身（「重命名实例」「新建分组」这些问一句名字的）。
     * 之所以不是另一个类：卡片、宽度、按钮行、Esc/回车的规矩这里全都已经有了，唯一多出来的
     * 就是这一格 —— 再写一个 `InlineInputBox` 等于把那一套抄第二遍。
     *
     * NOTE: 别在 `InlineMessageBox` 的 ctor 上加参数来做这件事：这一格**只有**"问名字"那一档
     *       要，而 ctor 的六个参数已经够长了。多一步 `setLineEdit()`，调用点一眼看得出它多
     *       要了什么。
     */
    QLineEdit *setLineEdit(const QString &initial);

    /// 那一行现在写的是什么。没插过输入框就是空串。
    QString lineEditText() const;

    /**
     * @brief 在正文下面、按钮行上面补一行**小字**（比正文小一号、颜色浅一档）。
     * @param text 那句话。**空串 = 不补**（视图里不留一行空的）。
     *
     * 「可以在设置里关掉」这一类**附注**：它必须看得见，但不该和正文抢注意力 ——
     * 正文是"现在要出什么事"，这一行是"以后不想看见它去哪儿关"。
     * 字号和颜色都取自本控件的调色板（`QPalette::PlaceholderText`，全仓"比正文浅一档"的
     * 唯一去处），所以换主题时它跟着走，不用在调用点各写一遍颜色。
     *
     * NOTE: 和 `setLineEdit()` 一样是**可选的一步**，不进 ctor：ctor 那六个参数已经够长了，
     *       而这一行只有"带附注"的那两处要。
     */
    void setNote(const QString &text);

    /// 那一行小字现在写的是什么。没设过就是空串。
    QString noteText() const;

    // ---------- objectName（验证工程靠它们认控件） ----------
    static QString titleLabelName() { return QStringLiteral("inlineMessageTitle"); }
    /// 插进来的那一行输入框。**只有"问名字"那一档有。**
    static QString lineEditName() { return QStringLiteral("inlineMessageEdit"); }
    /// 正文下面那一行小字。**只有走过 `setNote()` 的才有。**
    static QString noteLabelName() { return QStringLiteral("inlineMessageNote"); }
    static QString textLabelName() { return QStringLiteral("inlineMessageText"); }
    static QString buttonRowName() { return QStringLiteral("inlineMessageButtons"); }
    /// 第 index 颗按钮的名字。**位置是能变的**（按钮表由调用方给），所以认按钮要按下标，
    /// 不要按文字 —— 文字是显示内容，会被改。
    static QString buttonName(int index) {
        return QStringLiteral("inlineMessageButton%1").arg(index);
    }

protected:
    /**
     * @brief 摆上去之后把光标放回输入那一格（只有 `setLineEdit()` 插过的那一档有事做）。
     *
     * ★ 为什么非要在这儿补这一下：`setLineEdit()` 里的 `setFocus()` **不算数**。
     *   覆盖层摆卡片的时候会先 `setParent()`，而 Qt 的 `setParent()` 会把焦点清掉
     *   （它会让控件先 hide 一遍）；接着 `show()` 出来，QDialog 把焦点交给**默认按钮**
     *   （`QPushButton::setDefault(true)` 自己会抢一次）。症状很具体：用户点开"重命名"，
     *   直接打字 —— 字跑到"确定"那颗按钮上去，一个字符都不会进输入框。
     *   实测：不补这一下，验证工程量到的 `focusWidget()` 是 `inlineMessageButton0`。
     */
    void showEvent(QShowEvent *event) override;

private:
    int m_clicked = -1;
    MessageKind m_kind = MessageKind::Information;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_textLabel = nullptr;
    QLineEdit *m_lineEdit = nullptr;
    QLabel *m_noteLabel = nullptr;
};

namespace InlineMessage {

/// 报一句就走（`QMessageBox::information` 的替身）。
void information(OverlayHost *host, const QString &title, const QString &text);
/// 同上，warning 那一档。
void warning(OverlayHost *host, const QString &title, const QString &text);
/// 同上，critical 那一档。
void critical(OverlayHost *host, const QString &title, const QString &text);

/**
 * @brief 问一句（`QMessageBox::question(..., Yes | No, default)` 的替身）。
 * @param destructive "是"那一颗是不是**不可撤销**的（删除那一类）。**默认 false**。
 *        2026-09-28 起它真的会染色：`true` ⇒ 那颗按红（`ButtonKind::Danger`）、另一颗按
 *        黑（`Neutral`）。在此之前这一档只是一句记账。
 *
 *        ★ **标错比不标更糟**：标成 `true` 而其实能撤销，用户学会的是"红不代表什么"；
 *          真不可撤销的没标，红就白设了。判据只有一条 —— **点下去之后盘上少了一样
 *          用户自己找不回来的东西**。所以"从列表里移除文件夹""删除账号"里只有后者是红的
 *          （前者盘上那些文件一个都不动）。
 * @return 用户点了"是"那一颗。
 *
 * NOTE: 关掉 / 按 Esc / 点外面一律按**"否"**算 —— 和 QMessageBox 一样（Esc 走的是
 *       "escape button"）。调用点原来写的 `!= QMessageBox::Yes` 那半句要保留同样的方向。
 */
bool question(OverlayHost *host, const QString &title, const QString &text, const QString &yesText,
              const QString &noText, bool defaultYes, bool destructive = false);

/**
 * @brief 问一句名字（`QInputDialog::getText` 的替身）。
 * @param title 标题。
 * @param label 正文（"这个名字是干什么用的"那句话；可以带 `\n`）。
 * @param initial 输框里预填的内容。
 * @param ok 用户在不在"确定"那一颗上。**取消 / Esc / 点外面都是 false**（和 QInputDialog 一样）。
 * @return 输框里现在的内容。**`ok` 为 false 时也有值** —— 和 `QInputDialog::getText` 一样，
 *         调用点按老写法先看 `ok` 就行了。
 *
 * NOTE: 这一档和 `information()` 一样是**同步**的（内部一个嵌套事件循环），所以调用点
 *       `const QString x = getText(...)` 那一行的形状一个字没变。
 */
QString getText(OverlayHost *host, const QString &title, const QString &label,
                const QString &initial, bool *ok);

/**
 * @brief 按钮表自己定的那种问话（原来手工拼 `QMessageBox box(this)` + `addButton` 那两处）。
 * @param kind 哪一类（只影响标题颜色）。
 * @param buttons 按钮文字，从左到右。**空的话补一颗「确定」**。
 * @param defaultIndex 默认按钮下标，-1 = 没有（危险动作那两颗一律给 -1：误按回车不该把事办了）。
 * @param destructiveIndex 危险动作那一颗，-1 = 没有。**它会按红**（2026-09-28 起 ——
 *        在这之前它只记账不上色，写在这儿的那句"v1 只记账"就是这么过期掉的）。
 *        判据只有一条：**点下去之后盘上少了一样用户自己找不回来的东西**；其余按位置分档
 *        （默认那一颗 = 主题色，剩下的 = 黑），见 `InlineMessageBox` 的 ctor。
 * @param dismissOnOutsideClick 点框外面算不算"关掉它"。**默认 false**。
 * @param note 正文下面那行**小字**（`InlineMessageBox::setNote()`）。**空串 = 不补**。
 *        放在最后是因为它和上面那几格不是一类：上面几格决定"这个框是什么"，这一格只是
 *        附注一下 —— 两处用到它的调用点（启动前那两条确认）写的也都是"提醒本身 + 去哪儿关"。
 * @return 被点中的下标；关掉 / Esc / 点外面 = **-1**。
 */
int ask(OverlayHost *host, MessageKind kind, const QString &title, const QString &text,
        const QStringList &buttons, int defaultIndex = -1, int destructiveIndex = -1,
        bool dismissOnOutsideClick = false, const QString &note = QString());

}  // namespace InlineMessage
