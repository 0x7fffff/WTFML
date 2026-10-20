/**
 * @file AccountSkinDialog.hpp
 * @brief 「账号与外观」对话框：左边管多个正版账号（登录/切换/删除），右边管外观
 *        （换皮肤、换披风、不显示披风）。
 * @author 0x7fffff
 * @date 2026-09-22
 * @note 和 GroupDialog / InstanceSettingsDialog 同一条约定：**ui/ 不依赖业务层**，本文件
 *       不 include 任何 src/ 下的头；进参出参都是值，对话框只发"用户想做什么"的信号，
 *       真正的干活（登录、拉列表、上传）全在 MainWindow 里。
 *
 * 【为什么这个模块单独开一个对话框，而不是塞进设置页】
 *
 * 设置页里那一格是"给这个启动器选一个账户"，是**一次性**的决定；这里管的是"我有一堆
 * 账号、现在想换成哪个、顺手把这套外观换一下"——它是个**来回切**的地方。两类操作的节奏
 * 不一样，挤在一格里会出现"改完账号想去改皮肤，还得先记住在哪个标签页"。
 *
 * 【为什么披风只给一个列表，不给缩略图】
 *
 * 缩略图要按 url 去下（那是 PlayerSkinStore 的活），而"这件披风长什么样"看 3D 预览更准
 * ——预览里那个小人是**穿上**这一件的效果，比一排小图直观。列表上写名字就够了。
 *
 * 【错误怎么报】
 *
 * 这个对话框自己**不弹窗**（它连 QMessageBox 都不 include）：正文由 MainWindow 那边
 * 决定（用户主动发起的动作失败 → 弹窗；背景刷新失败 → 写进这里的 statusLabel，见
 * MainWindow::refreshAccountSkin()）。分开的理由是这两种失败的"该不该打断用户"不一样。
 */

#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
#include <QString>

class QComboBox;
class QBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class SkinViewer;

class AccountSkinDialog : public QDialog {
    Q_OBJECT

public:
    /// 列表里一个账号。**只带显示要用的东西**，凭据字节在 AccountStore 里，不出业务层。
    struct AccountRow {
        QString uuid;
        QString name;
        /// 收进列表的时刻（ISO）。显示成"存于 …"，用户认得出是哪一个。
        QString savedAt;
        bool current = false;
        /// 这是一条**离线账号**（只有名字，盘上没有凭据）。行尾会挂「（离线）」。
        bool offline = false;
    };

    /// 一件披风。
    struct CapeRow {
        QString id;
        QString alias;
        bool active = false;
    };

    explicit AccountSkinDialog(QWidget *parent = nullptr);

    // ---------- 数据进 ----------

    /// 重建账号列表。当前账号那一条会被选中（列表空/没当前账号时选中第一条）。
    /// NOTE: 选中项**故意不跨调用保留**：这一列会被刷新好几遍（登录成功、切换、删除），
    ///       保留一个 uuid 就得处理"它已经不在列表里了"，而用户看的是"当前账号是哪一条"。
    void setAccounts(const QList<AccountRow> &rows);

    /// 当前身份那一句（"已登录为「X」（正版登录）" / "已登录为「X」（离线登录）" /
    /// "还没登录"）。正文由调用方给（它才知道当前账号是哪种），这里只负责显示。
    void setStatusText(const QString &text);

    /// 3D 预览那套外观。**和主页右栏是同一份贴图**（同一份数据两个窗口）。
    /// @param skin 皮肤贴图（64×64 或老格式 64×32）。空图 = 只画背景。
    /// @param cape 披风贴图（64×32）。可空。
    /// @param slim 是不是纤细（Alex）模型。
    void setLooks(const QImage &skin, const QImage &cape, bool slim);

    /// 重建披风列表（"不显示披风"是列表下面那个按钮，不占一行）。
    void setCapes(const QList<CapeRow> &capes);

    /// 有一件请求在走（登录中、正在拉列表）。按钮一起灰掉 —— 同步请求期间界面本来就
    /// 点不动，这一句是给**登录**那条异步路用的（浏览器可能几十秒才回来）。
    void setBusy(bool busy, const QString &what = QString());

    /**
     * @brief 当前账号是不是离线身份（决定「皮肤」那一行露出哪一颗按钮）。
     *
     * 正版账号走「上传皮肤…」（要令牌、传到官方）；离线账号没有令牌，那一颗按下去只会
     * 失败，所以**藏起来**，换成「换一张…」（从盘上挑一张，见 coding plan §1.4 那条缺口）。
     * 两档互斥，不是两个都能点。
     *
     * @param offline 当前账号本身是不是离线账号（判据由调用方给，见
     *        `PlayerSkinController::offlineIdentity()` —— 全仓唯一出处）。
     * @param hasCustomSkin 这个离线账号现在是不是已经有一张自己挑的了 ——
     *        「恢复默认外观」只在有东西可恢复时可用（没有就灰着，点了没反应比灰着更难懂）。
     */
    void setOfflineSkinState(bool offline, bool hasCustomSkin);

    // ---------- 数据出（现场读控件，没有影子状态） ----------

    /// 账号列表里选中那一条的 uuid（没选中返回空串）。
    QString selectedUuid() const;
    /// 披风列表里选中那一件的 id（没选中返回空串）。
    QString selectedCapeId() const;
    /// 「模型」下拉框里选的 variant（已经是小写形态：classic / slim）。
    QString selectedVariant() const;

    /// 把离线名字那个输入框清空。
    /// NOTE: **只能由调用方在添加成功之后调**，别塞进 setAccounts() —— 那个函数是"刷新
    ///       列表"的意思，用户正打字时来一次刷新就把没提交的名字擦掉了。
    void clearOfflineNameDraft();

    // ---------- 控件的 objectName ----------
    // 验证工程靠这些名字认控件，别改回按文字去认（文字会随数据变）。
    static QString statusLabelName() { return QStringLiteral("accountSkinStatus"); }
    static QString accountListName() { return QStringLiteral("accountSkinAccountList"); }
    static QString loginButtonName() { return QStringLiteral("accountSkinLoginButton"); }
    static QString switchButtonName() { return QStringLiteral("accountSkinSwitchButton"); }
    static QString removeButtonName() { return QStringLiteral("accountSkinRemoveButton"); }
    static QString refreshButtonName() { return QStringLiteral("accountSkinRefreshButton"); }
    static QString offlineNameEditName() { return QStringLiteral("accountSkinOfflineNameEdit"); }
    static QString addOfflineButtonName() {
        return QStringLiteral("accountSkinAddOfflineButton");
    }
    static QString viewerName() { return QStringLiteral("accountSkinViewer"); }
    static QString uploadButtonName() { return QStringLiteral("accountSkinUploadButton"); }
    /// 离线账号那一颗「换一张…」（正版账号露的是 uploadButtonName()，两档互斥）。
    static QString offlineSkinButtonName() {
        return QStringLiteral("accountSkinOfflineSkinButton");
    }
    /// 离线账号那一颗「恢复默认外观」（撤掉自己挑的那张）。
    static QString offlineSkinResetButtonName() {
        return QStringLiteral("accountSkinOfflineSkinResetButton");
    }
    static QString variantComboName() { return QStringLiteral("accountSkinVariantCombo"); }
    static QString capeListName() { return QStringLiteral("accountSkinCapeList"); }
    static QString applyCapeButtonName() { return QStringLiteral("accountSkinApplyCapeButton"); }
    static QString hideCapeButtonName() { return QStringLiteral("accountSkinHideCapeButton"); }
    static QString closeButtonName() { return QStringLiteral("accountSkinCloseButton"); }

    // ---------- 产品文案 ----------
    static QString windowTitle() { return QStringLiteral("账号与外观"); }
    static QString accountSectionTitle() { return QStringLiteral("账号（正版 / 离线）"); }
    static QString looksSectionTitle() { return QStringLiteral("外观预览"); }
    static QString capeSectionTitle() { return QStringLiteral("披风"); }
    static QString loginButtonText() { return QStringLiteral("登录新账号…"); }
    static QString switchButtonText() { return QStringLiteral("切换到所选"); }
    static QString removeButtonText() { return QStringLiteral("删除所选"); }
    static QString refreshButtonText() { return QStringLiteral("刷新外观"); }
    static QString uploadButtonText() { return QStringLiteral("上传皮肤…"); }
    /// 离线账号那一颗（见 `setOfflineSkinState()`）。用词和图片选择框/Coding plan 里那句
    /// 「换一张」一致 —— 它换的只是**启动器里**显示的那张，不是游戏里那张脸。
    static QString offlineSkinButtonText() { return QStringLiteral("换一张…"); }
    static QString offlineSkinResetButtonText() { return QStringLiteral("恢复默认外观"); }
    static QString offlineSkinButtonToolTip() {
        return QStringLiteral(
            "从盘上挑一张 PNG（64×64 或 64×32）当这个离线账号在启动器里的外观。"
            "只影响启动器里的预览，游戏里那张脸是游戏自己定的。");
    }
    static QString offlineSkinResetButtonToolTip() {
        return QStringLiteral("撤掉自己挑的那张，回到按这个账号算出来的 / 随机挑的那一张。");
    }
    static QString applyCapeButtonText() { return QStringLiteral("显示所选披风"); }
    static QString hideCapeButtonText() { return QStringLiteral("不显示披风"); }
    static QString closeButtonText() { return QStringLiteral("关闭"); }
    static QString variantClassicText() { return QStringLiteral("经典（Steve）"); }
    static QString variantSlimText() { return QStringLiteral("纤细（Alex）"); }
    /// 模型下拉框里那一项对应的 variant 值（**小写**，就是传给官方那个）。
    static QString variantClassicValue() { return QStringLiteral("classic"); }
    static QString variantSlimValue() { return QStringLiteral("slim"); }
    /// 当前账号那一条后面的标记。
    static QString currentMark() { return QStringLiteral("（当前）"); }
    /// 离线账号那一条后面的标记（和「（当前）」可以同时出现）。
    static QString offlineMark() { return QStringLiteral("（离线）"); }
    static QString offlineNamePlaceholder() { return QStringLiteral("离线账号的名字"); }
    static QString addOfflineButtonText() { return QStringLiteral("添加离线账号"); }
    /// 离线账号那一行的 tooltip —— 它没有"什么时候登录取自"这回事，得说清它是哪一种。
    static QString offlineRowTooltip() {
        return QStringLiteral("离线账号：盘上没有它的登录凭据，启动时用这个名字离线进游戏。");
    }
    /// 当前身份那一句，离线那一档。和 identityText() 是**两句不同的话** —— 离线账号
    /// 不是"正版登录"，这两个词在用户那里是完全不同的两件事，不能混用一句。
    /// @param name 离线账号的名字。
    static QString offlineIdentityText(const QString &name) {
        return QStringLiteral("已登录为「%1」（离线登录）").arg(name);
    }
    /// 这一列只在有账号时才有内容，空的时候给一句说明而不是一片空白。
    static QString noAccountsText() { return QStringLiteral("还没有保存的账号。"); }
    static QString notLoggedInNote() { return QStringLiteral("还没登录。点「登录新账号…」开始。"); }
    static QString loginHintNote() {
        return QStringLiteral("正版账号走微软登录（会打开浏览器）；离线账号在下面填个名字就行。");
    }
    /// 当前身份那一句。**只在这一种情况下用**：当前账号是个正版（微软）账号。
    /// 另外两种（当前是离线账号 / 没登录）各自有专门那句，见 offlineIdentityText() /
    /// notLoggedInNote()。
    ///
    /// NOTE: 2026-09-24 之前这里还有第三档 —— 「勾着「离线模式」但账号是正版的」，
    ///       对应的是主页那个勾选框。那个勾选框去掉了（用户原话：「现在不要启动界面那个
    ///       离线登录勾选框了，在添加账户那里选择添加离线账号」），于是"离线"只剩一种
    ///       意思：**当前账号本身是离线账号**。那一档连同 offlineModeNote() 一起作废。
    /// @param name 账号名。
    static QString identityText(const QString &name) {
        return QStringLiteral("已登录为「%1」（正版登录）").arg(name);
    }

signals:
    /// 用户要加一个账号（开始走微软登录）。
    void loginRequested();
    /// 用户填了个离线名字要加进来。
    /// @param name 已经 trim 过的名字（合不合法由接收方判，见文件头那两条约定）。
    void offlineAccountRequested(const QString &name);
    /// 用户要切换到某个账号（列表里那一行的 uuid）。
    void switchRequested(const QString &uuid);
    /// 用户要删掉某个账号。
    void removeRequested(const QString &uuid);
    /// 用户要重新拉一遍外观（皮肤/披风/账号列表）。
    void refreshRequested();
    /// 用户选好了要上传的皮肤文件。
    /// @param path 本地 PNG 路径。@param variant 想用哪个模型（classic / slim）。
    void skinUploadRequested(const QString &path, const QString &variant);
    /// 离线账号：用户挑了一张本地皮肤当"启动器里那张脸"。
    /// @param path 本地图片路径。@param variant 模型（classic / slim）。
    ///
    /// NOTE: 和 `skinUploadRequested()` **分开**：那个是传给 Mojang（要令牌、改的是
    ///       官方档案），这个是只存在本机（不需要令牌、改的是启动器里的预览）。一个信号
    ///       两件事的话，接收方就得按"当前是不是离线"再分一次 —— 那一分迟早分错。
    void offlineSkinPickRequested(const QString &path, const QString &variant);
    /// 离线账号：撤掉自己挑的那张。
    void offlineSkinResetRequested();
    /// 用户要把某一件披风设成当前。
    void capeSelectRequested(const QString &capeId);
    /// 用户要取消披风（不显示）。
    void hideCapeRequested();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    /// 「上传皮肤…」：选文件，成了就发 skinUploadRequested()。
    void pickSkinFile();
    /// 「换一张…」（只有离线账号露这一颗）：选文件，发 offlineSkinPickRequested()。
    void pickOfflineSkinFile();
    /// 「添加离线账号」（或者在那个输入框里回车）：发 offlineAccountRequested()。
    void submitOfflineName();
    /// 列表选择变化时刷那几个按钮的可用性（没选中就点不动）。
    void refreshButtonStates();

private:
    /// 往列表里塞一行（文字 + uuid + 当前标记）。
    void addAccountRow(const AccountRow &row);

    /// 现在是不是有一件请求在走。**它必须参与** refreshButtonStates() 的判断：
    /// 那个函数是按「有没有选中」定可用性的，而 setBusy() 的末尾会调它一次 —— 于是
    /// 忙的时候那几个按钮会被重新点亮，等于登录还在飞用户就能切账号，回来之后两条路
    /// 各写一遍凭据。这个成员就是给那个函数补上「忙的时候一律灰」这一条。
    bool m_busy = false;
    QWidget *m_headerBar = nullptr;
    QWidget *m_accountsPanel = nullptr;
    QBoxLayout *m_bodyLayout = nullptr;

    /// 「皮肤」那一行现在露的是哪一档（见 setOfflineSkinState()）。两个都从外面喂进来，
    /// 这个窗不自己判"当前账号是不是离线" —— 那件事的唯一出处在业务层。
    bool m_offlineSkin = false;
    bool m_hasCustomSkin = false;

    QLabel *m_statusLabel = nullptr;
    QListWidget *m_accountList = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_switchButton = nullptr;
    QPushButton *m_removeButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QLineEdit *m_offlineNameEdit = nullptr;
    QPushButton *m_addOfflineButton = nullptr;
    SkinViewer *m_viewer = nullptr;
    QPushButton *m_uploadButton = nullptr;
    /// 离线账号那一档的两颗（默认都藏着，见 setOfflineSkinState()）。
    QPushButton *m_offlineSkinButton = nullptr;
    QPushButton *m_offlineSkinResetButton = nullptr;
    QComboBox *m_variantCombo = nullptr;
    QListWidget *m_capeList = nullptr;
    QPushButton *m_applyCapeButton = nullptr;
    QPushButton *m_hideCapeButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
