/**
 * @file AccountSkinDialog.cpp
 * @brief 「账号与外观」对话框（见 .hpp 里那几段说明）。
 */

#include "AccountSkinDialog.hpp"

// WheelGuard 住在 MainWindowUI.hpp 里（和 InstanceSettingsDialog.cpp 一样，直接引那一份，
// 不另外抄一个防护类）。它护的是下面那个「模型」下拉框。
#include "MainWindowUI.hpp"
#include "SkinModel.hpp"
#include "Theme.hpp"   // 这一窗里「删除所选」那一颗是危险按钮（红字红边，见构造函数）
#include "SkinViewer.hpp"
#include "OverlayHost.hpp"
#include <QShowEvent>
#include <QResizeEvent>

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int kListMinHeight = 96;
constexpr int kInitialWidth = 980;
constexpr int kInitialHeight = 460;
}  // namespace

AccountSkinDialog::AccountSkinDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(windowTitle());
    setObjectName(QStringLiteral("accountSkinDialog"));
    // 尺寸**不**跟着内容长：这个窗里的东西是固定的四块，摆好之后让用户自己拉。
    resize(kInitialWidth, kInitialHeight);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(12);
    root->setAlignment(Qt::AlignTop);

    // ---------- 顶部：当前身份 / 最近一次动作的结果 ----------
    m_statusLabel = new QLabel(notLoggedInNote(), this);
    m_statusLabel->setObjectName(statusLabelName());
    m_statusLabel->setWordWrap(false);
    m_statusLabel->setMaximumWidth(480);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_headerBar = new QWidget(this);
    m_headerBar->setObjectName(QStringLiteral("accountHeaderBar"));
    m_headerBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *statusRow = new QHBoxLayout(m_headerBar);
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->addWidget(m_statusLabel, 1);
    root->addWidget(m_headerBar);

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("accountBody"));
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QHBoxLayout *main = new QHBoxLayout(body);
    main->setContentsMargins(0, 0, 0, 0);
    main->setSpacing(24);
    m_bodyLayout = main;
    root->addWidget(body);

    // ---------- 左：账号 ----------
    m_accountsPanel = new QWidget(this);
    m_accountsPanel->setObjectName(QStringLiteral("accountManagementPane"));
    m_accountsPanel->setMaximumWidth(340);
    QVBoxLayout *left = new QVBoxLayout(m_accountsPanel);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(8);
    left->setAlignment(Qt::AlignTop);
    main->addWidget(m_accountsPanel, 3, Qt::AlignTop);

    QLabel *accountTitle = new QLabel(accountSectionTitle(), this);
    QFont sectionFont = accountTitle->font();
    sectionFont.setBold(true);
    accountTitle->setFont(sectionFont);
    left->addWidget(accountTitle);

    // 标题 → 四个按钮 → 离线那一行 → 列表（吃掉剩下的高度）→ 登录说明
    //
    // 按钮贴着标题，列表在它下面长：按钮压在列表底下的话，列表一长整窗就往下坠，
    // 加账号还得先往下找。
    //
    // NOTE: **建控件**的次序和下面 `addWidget()` / `addLayout()` 的次序是两回事 ——
    // Qt 里"摆在哪"只由后者说了算，new 的先后不影响。
    // 所以这一段和下面那两处 `addWidget`/`addLayout` 是**同一件事的两半**，
    // 改一头必须改另一头（`m_accountList` 和 `loginHint` 是在下面才 add 的）。
    m_accountList = new QListWidget(this);
    m_accountList->setObjectName(accountListName());
    m_accountList->setMinimumHeight(kListMinHeight);
    m_accountList->setMaximumHeight(144);


    QGridLayout *accountButtons = new QGridLayout();
    left->addLayout(accountButtons);
    m_loginButton = new QPushButton(loginButtonText(), this);
    m_loginButton->setObjectName(loginButtonName());
    m_loginButton->setToolTip(loginHintNote());
    m_switchButton = new QPushButton(switchButtonText(), this);
    m_switchButton->setObjectName(switchButtonName());
    // 「用这个账号」= 换个身份（一般性那一档）。
    Theme::markButton(m_switchButton, Theme::ButtonKind::Neutral);
    m_removeButton = new QPushButton(removeButtonText(), this);
    m_removeButton->setObjectName(removeButtonName());
    // 「删除所选」是这一窗里唯一一处**删东西**的按钮（账号删了就没了），所以它走那一档
    // **红字红边**（走 `Theme::ButtonKind::Danger`）。
    // NOTE: 挂属性就够，样式表那边按属性选中（见 `Theme::buttonStyleSheet()`）——
    // 属性要在**第一次 polish 之前**挂上，所以是紧挨着 new 的这一行。
    Theme::markButton(m_removeButton, Theme::ButtonKind::Danger);
    m_refreshButton = new QPushButton(refreshButtonText(), this);
    m_refreshButton->setObjectName(refreshButtonName());
    // 「重新取一次」= 刷新（一般性那一档）。
    Theme::markButton(m_refreshButton, Theme::ButtonKind::Ghost);
    accountButtons->addWidget(m_loginButton, 0, 0, 1, 2);
    accountButtons->addWidget(m_switchButton, 1, 0);
    accountButtons->addWidget(m_removeButton, 1, 1);
    statusRow->addWidget(m_refreshButton);
    // 「刷新外观」和账号那三个按钮排在一行：它刷的是**这一整个窗**（账号列表 + 皮肤 +
    // 披风，见 MainWindow::refreshAccountSkin()），不是只管右边那一栏。
    Theme::markButton(m_loginButton, Theme::ButtonKind::Primary);

    // 离线账号那一路。和上面那排按钮分开摆，是因为它**不是**对"选中项"做事（不依赖列表
    // 里选中了谁），而是一个"加东西"的入口 —— 和「登录新账号…」地位相当，只是不用浏览器。
    QHBoxLayout *offlineRow = new QHBoxLayout();
    left->addLayout(offlineRow);
    m_offlineNameEdit = new QLineEdit(this);
    m_offlineNameEdit->setObjectName(offlineNameEditName());
    m_offlineNameEdit->setPlaceholderText(offlineNamePlaceholder());
    m_addOfflineButton = new QPushButton(addOfflineButtonText(), this);
    m_addOfflineButton->setObjectName(addOfflineButtonName());
    offlineRow->addWidget(m_offlineNameEdit, 1);
    offlineRow->addWidget(m_addOfflineButton);

    // 上面那几行都是固定高的，**列表吃剩下的高度**（窗口拉高拉矮只动它这一块）。
    left->addWidget(m_accountList, 1);
    // 说明摆最后 —— 它是注释，不是内容（见这上面 hintPalette 那几句）。

    // ---------- 右：外观 ----------
    QVBoxLayout *right = new QVBoxLayout();
    right->setSpacing(8);
    right->setAlignment(Qt::AlignTop);
    main->addLayout(right, 6);

    QLabel *looksTitle = new QLabel(looksSectionTitle(), this);
    looksTitle->setFont(sectionFont);
    right->addWidget(looksTitle);
    auto *appearance = new QHBoxLayout;
    appearance->setSpacing(16);
    right->addLayout(appearance, 1);

    m_viewer = new SkinViewer(this);
    m_viewer->setObjectName(viewerName());
    m_viewer->setFixedSize(208, 312);
    m_viewer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // 一进来就有东西可看：没登录、还没取到、取不到，看到的都是这套默认外观
    // （和主页右栏同一个做法 —— 那个控件任何时候都不该是空白的）。
    m_viewer->setSkin(SkinModel::defaultSteveSkin(), QImage(), false);
    appearance->addWidget(m_viewer, 0, Qt::AlignTop);
    auto *controls = new QVBoxLayout;
    controls->setSpacing(8);
    controls->setAlignment(Qt::AlignTop);
    appearance->addLayout(controls, 1);

    QVBoxLayout *uploadRow = new QVBoxLayout();
    controls->addWidget(new QLabel(tr("皮肤"), this));
    controls->addLayout(uploadRow);
    m_uploadButton = new QPushButton(uploadButtonText(), this);
    m_uploadButton->setObjectName(uploadButtonName());
    m_variantCombo = new QComboBox(this);
    m_variantCombo->setObjectName(variantComboName());
    m_variantCombo->addItem(variantClassicText(), variantClassicValue());
    m_variantCombo->addItem(variantSlimText(), variantSlimValue());
    // 滚轮防护（见 MainWindowUI.hpp 里的 WheelGuard）：它的后果
    // 和设置页那几个不一样，不是"显示不对"而是"传错了"：鼠标从它上面划过去会静默换成
    // 另一种模型，接着点「上传」传上去的就是错的那种（经典/纤细在游戏里是两套手臂宽度）。
    WheelGuard::install(m_variantCombo);
    uploadRow->addWidget(m_uploadButton);
    uploadRow->addWidget(m_variantCombo);

    // ---------- 离线账号那一档：换一张 / 恢复默认 ----------
    // 这两颗和上面那颗「上传皮肤…」互斥（同一行里只露一档，见 setOfflineSkinState()）：
    // 离线账号没有令牌，上传那一颗按下去只会失败；它要的是"从盘上挑一张"。
    // 默认藏着 —— 这个窗第一次画出来时还没人喂"当前是不是离线"（那是业务层的事）。
    auto *offlineSkinRow = new QHBoxLayout();
    offlineSkinRow->setContentsMargins(0, 0, 0, 0);
    m_offlineSkinButton = new QPushButton(offlineSkinButtonText(), this);
    m_offlineSkinButton->setObjectName(offlineSkinButtonName());
    m_offlineSkinButton->setAutoDefault(false);
    m_offlineSkinButton->setToolTip(offlineSkinButtonToolTip());
    m_offlineSkinResetButton = new QPushButton(offlineSkinResetButtonText(), this);
    m_offlineSkinResetButton->setObjectName(offlineSkinResetButtonName());
    m_offlineSkinResetButton->setAutoDefault(false);
    m_offlineSkinResetButton->setToolTip(offlineSkinResetButtonToolTip());
    // 「恢复默认外观」= 撤掉一层（和「不显示披风」同一档）；「换一张…」是正常操作。
    Theme::markButton(m_offlineSkinResetButton, Theme::ButtonKind::Ghost);
    offlineSkinRow->addWidget(m_offlineSkinButton);
    offlineSkinRow->addWidget(m_offlineSkinResetButton);
    controls->addLayout(offlineSkinRow);
    m_offlineSkinButton->hide();
    m_offlineSkinResetButton->hide();


    QLabel *capeTitle = new QLabel(capeSectionTitle(), this);
    capeTitle->setFont(sectionFont);
    controls->addSpacing(8);
    controls->addWidget(capeTitle);

    m_capeList = new QListWidget(this);
    m_capeList->setObjectName(capeListName());
    m_capeList->setMinimumHeight(72);
    m_capeList->setMaximumHeight(132);
    controls->addWidget(m_capeList, 1);

    QHBoxLayout *capeButtons = new QHBoxLayout();
    controls->addLayout(capeButtons);
    m_applyCapeButton = new QPushButton(applyCapeButtonText(), this);
    m_applyCapeButton->setObjectName(applyCapeButtonName());
    m_applyCapeButton->setText(tr("应用"));
    m_applyCapeButton->setToolTip(applyCapeButtonText());
    m_hideCapeButton = new QPushButton(hideCapeButtonText(), this);
    m_hideCapeButton->setObjectName(hideCapeButtonName());
    m_hideCapeButton->setText(tr("隐藏"));
    m_hideCapeButton->setToolTip(hideCapeButtonText());
    // 「不显示披风」= 拿掉一层（和「清除图片」同一档）；旁边那颗「应用披风」是确认性。
    Theme::markButton(m_hideCapeButton, Theme::ButtonKind::Ghost);
    capeButtons->addWidget(m_applyCapeButton);
    capeButtons->addWidget(m_hideCapeButton);

    // ---------- 底：关闭 ----------
    // NOTE: 只有「关闭」，没有「确定」—— 这个窗里的每一件事（切换、上传、换披风）**当场
    // 就生效**，没有"确定之后才写盘"这一层。摆一个"确定"会让用户以为不点它就不算数。
    QHBoxLayout *bottom = new QHBoxLayout();
    root->addLayout(bottom);
    bottom->addStretch(1);
    m_closeButton = new QPushButton(closeButtonText(), this);
    m_closeButton->setObjectName(closeButtonName());
    // 「关闭」是这个窗里唯一一颗"什么也不改"的按钮 ⇒ 次要那一档（黑字黑边）。这样一窗
    // 里三档就齐了：绿的是正常操作（登录/切换/刷新）、黑的是退出来、红的是删东西。
    Theme::markButton(m_closeButton, Theme::ButtonKind::Neutral);
    bottom->addWidget(m_closeButton);

    // ---------- 接线 ----------
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_loginButton, &QPushButton::clicked, this, &AccountSkinDialog::loginRequested);
    connect(m_addOfflineButton, &QPushButton::clicked, this,
            &AccountSkinDialog::submitOfflineName);
    // 在框里直接回车 = 点那个按钮。少一次"打完名字还要去够鼠标"。
    connect(m_offlineNameEdit, &QLineEdit::returnPressed, this,
            &AccountSkinDialog::submitOfflineName);
    connect(m_refreshButton, &QPushButton::clicked, this, &AccountSkinDialog::refreshRequested);
    connect(m_uploadButton, &QPushButton::clicked, this, &AccountSkinDialog::pickSkinFile);
    connect(m_offlineSkinButton, &QPushButton::clicked, this,
            &AccountSkinDialog::pickOfflineSkinFile);
    connect(m_offlineSkinResetButton, &QPushButton::clicked, this,
            &AccountSkinDialog::offlineSkinResetRequested);
    connect(m_accountList, &QListWidget::itemSelectionChanged, this,
            &AccountSkinDialog::refreshButtonStates);
    connect(m_capeList, &QListWidget::itemSelectionChanged, this,
            &AccountSkinDialog::refreshButtonStates);
    connect(m_switchButton, &QPushButton::clicked, this, [this] {
        const QString uuid = selectedUuid();
        if (!uuid.isEmpty()) {
            emit switchRequested(uuid);
        }
    });
    connect(m_removeButton, &QPushButton::clicked, this, [this] {
        const QString uuid = selectedUuid();
        if (!uuid.isEmpty()) {
            emit removeRequested(uuid);
        }
    });
    connect(m_applyCapeButton, &QPushButton::clicked, this, [this] {
        const QString id = selectedCapeId();
        if (!id.isEmpty()) {
            emit capeSelectRequested(id);
        }
    });
    connect(m_hideCapeButton, &QPushButton::clicked, this,
            &AccountSkinDialog::hideCapeRequested);

    refreshButtonStates();
}

void AccountSkinDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    OverlayHost *host = nullptr;
    for (auto *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if ((host = qobject_cast<OverlayHost *>(ancestor))) break;
    }
    if (host && host->isOpen()) {
        layout()->removeWidget(m_headerBar);
        host->setPageHeaderWidget(m_headerBar);
        m_closeButton->hide(); // 内嵌页已有返回，不占一整排页脚。
    } else {
        qobject_cast<QVBoxLayout *>(layout())->insertWidget(0, m_headerBar);
        m_headerBar->show();
        m_closeButton->show();
    }
}

void AccountSkinDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    const bool narrow = width() < 860;
    m_bodyLayout->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_accountsPanel->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 340);
}

void AccountSkinDialog::setAccounts(const QList<AccountRow> &rows) {
    m_accountList->clear();
    m_accountList->setMaximumHeight(qBound(96, int(rows.size()) * (fontMetrics().height() + 12), 192));
    for (const AccountRow &row : rows) {
        addAccountRow(row);
    }

    if (m_accountList->count() == 0) {
        // 一个账号都没有时摆一句话，而不是一片空白：空白让人以为"还没加载完"。
        QListWidgetItem *placeholder = new QListWidgetItem(noAccountsText(), m_accountList);
        placeholder->setFlags(Qt::NoItemFlags);  // 选不动 —— 它不是一个账号
        placeholder->setForeground(palette().color(QPalette::PlaceholderText));
    } else {
        // 选中"当前"那一条；没有当前账号（凭据被清了）就选第一条。
        int index = 0;
        for (int i = 0; i < m_accountList->count(); ++i) {
            if (m_accountList->item(i)->data(Qt::UserRole + 1).toBool()) {
                index = i;
                break;
            }
        }
        m_accountList->setCurrentRow(index);
    }
    refreshButtonStates();
}

void AccountSkinDialog::addAccountRow(const AccountRow &row) {
    QListWidgetItem *item = new QListWidgetItem(m_accountList);
    // 两个标记可能同时出现（一个离线账号正被用着）：离线那句在前 —— 它说的是"这是个什么
    // 账号"，"当前"是它现在的状态。
    QString text = row.name;
    if (row.offline) {
        text += offlineMark();
    }
    if (row.current) {
        text += currentMark();
    }
    item->setText(text);
    item->setData(Qt::UserRole, row.uuid);
    item->setData(Qt::UserRole + 1, row.current);
    if (row.offline) {
        // 离线账号没有"登录取自什么时候"这回事 —— 摆一个空的 tooltip 还不如说清它是哪一种。
        item->setToolTip(offlineRowTooltip());
    } else if (!row.savedAt.isEmpty()) {
        const QDateTime savedAt = QDateTime::fromString(row.savedAt, Qt::ISODate);
        if (savedAt.isValid()) {
            item->setToolTip(tr("登录取自 %1")
                                 .arg(savedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        }
    }
}

void AccountSkinDialog::clearOfflineNameDraft() { m_offlineNameEdit->clear(); }

void AccountSkinDialog::setStatusText(const QString &text) {
    m_statusLabel->setText(text);
    m_statusLabel->setToolTip(text);
}

void AccountSkinDialog::setLooks(const QImage &skin, const QImage &cape, bool slim) {
    m_viewer->setSkin(skin.isNull() ? SkinModel::defaultSteveSkin() : skin, cape, slim);
    // 下拉框跟着**当前皮肤**走：用户多半是"在这个模型上调一下"，而不是每次都从头选。
    const QString variant = slim ? variantSlimValue() : variantClassicValue();
    const int index = m_variantCombo->findData(variant);
    if (index >= 0) {
        m_variantCombo->setCurrentIndex(index);
    }
}

void AccountSkinDialog::setCapes(const QList<CapeRow> &capes) {
    m_capeList->clear();
    for (const CapeRow &cape : capes) {
        QListWidgetItem *item = new QListWidgetItem(m_capeList);
        item->setText(cape.active ? cape.alias + currentMark() : cape.alias);
        item->setData(Qt::UserRole, cape.id);
        item->setData(Qt::UserRole + 1, cape.active);
    }
    refreshButtonStates();
}

void AccountSkinDialog::setBusy(bool busy, const QString &what) {
    m_busy = busy;
    m_loginButton->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
    m_uploadButton->setEnabled(!busy);
    // 离线那一档的两颗一起灰（忙的时候下面 refreshButtonStates() 也会按 idle 重算一遍）。
    m_offlineSkinButton->setEnabled(!busy);
    m_offlineSkinResetButton->setEnabled(!busy);
    // 两个列表一起灰：它们的内容正是「正在被刷新」的那两样（账号列表和披风列表都在
    // refreshAccountSkin() 里重填），忙的时候让用户改选中项，只会让「我点的是哪个」
    // 和「回来的结果是哪个」对不上。
    m_accountList->setEnabled(!busy);
    m_capeList->setEnabled(!busy);
    if (busy) {
        m_statusLabel->setText(what);
    }
    refreshButtonStates();
}

void AccountSkinDialog::setOfflineSkinState(bool offline, bool hasCustomSkin) {
    m_offlineSkin = offline;
    m_hasCustomSkin = hasCustomSkin;
    m_uploadButton->setVisible(!offline);
    m_offlineSkinButton->setVisible(offline);
    m_offlineSkinResetButton->setVisible(offline);
    refreshButtonStates();
}

QString AccountSkinDialog::selectedUuid() const {
    const QListWidgetItem *item = m_accountList->currentItem();
    if (item == nullptr || !item->flags().testFlag(Qt::ItemIsSelectable)) {
        return QString();  // 占位那一行选不动，见 setAccounts()
    }
    return item->data(Qt::UserRole).toString();
}

QString AccountSkinDialog::selectedCapeId() const {
    const QListWidgetItem *item = m_capeList->currentItem();
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

QString AccountSkinDialog::selectedVariant() const {
    const QString value = m_variantCombo->currentData().toString();
    return value.isEmpty() ? variantClassicValue() : value;
}

void AccountSkinDialog::pickSkinFile() {
    // NOTE: 这一处也是独立窗（Windows 自己的文件选择框）—— 理由和取舍见
    // MainWindow::onAddFolderClicked() 上面那一段。
    const QString path = QFileDialog::getOpenFileName(this, tr("选择皮肤图片"), QString(),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    // NOTE: 这里**只**把路径交出去，不自己校验尺寸 —— 尺寸那条规矩在
    // SkinApi::isAcceptableSkinSize() 里（唯一出处），校验失败由 MainWindow 弹窗说。
    // 在这个窗里也判一遍的话，会出现"这里说不行、那边说行"的两份判据。
    emit skinUploadRequested(path, selectedVariant());
}

void AccountSkinDialog::pickOfflineSkinFile() {
    // 和上面那颗同一个选择框；区别只在**交出去的那件事**是什么（见头文件那两个信号）。
    const QString path = QFileDialog::getOpenFileName(this, tr("选择皮肤图片"), QString(),
                                                      tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) {
        return;  // 用户取消了，不是错误
    }
    // 尺寸那条规矩同样不在这里判（唯一出处 SkinApi::isAcceptableSkinSize()，由 MainWindow 弹窗说）。
    emit offlineSkinPickRequested(path, selectedVariant());
}

void AccountSkinDialog::submitOfflineName() {
    // 名字合不合法**不在这里判**（空、有空白、太长都在 AccountStore::addOffline() 里，
    // 唯一出处）：在这个窗里也判一遍就会出现"这里说不行、那边说行"两份判据，而这里判出来
    // 的话术还得和那边保持一致。这里只管把用户填的东西交出去。
    emit offlineAccountRequested(m_offlineNameEdit->text().trimmed());
}

void AccountSkinDialog::refreshButtonStates() {
    // 「切换到所选」「删除所选」「显示所选披风」三件事都**要有一个选中的东西**才做得了。
    // 没选中就灰着：点了没反应比灰着更难理解（用户会以为功能坏了）。
    // NOTE: 三个都要**同时**看「有没有选中」和「忙不忙」。只看选中那一半的话，
    // setBusy(true) 里末尾那次调用会把它们重新点亮（见 .hpp 里 m_busy 那段）。
    const bool idle = !m_busy;
    const bool haveAccount = !selectedUuid().isEmpty();
    m_switchButton->setEnabled(idle && haveAccount);
    m_removeButton->setEnabled(idle && haveAccount);
    m_applyCapeButton->setEnabled(idle && !selectedCapeId().isEmpty());
    // 加离线账号要跟着"忙不忙"灰：它一旦成功就会把活动槽清空（切到离线身份），而正飞着的
    // 那次登录回来还要往活动槽里写 —— 两个人会打架。这一句和上面三条同一个理由，只是它
    // 不看"有没有选中"（加东西不依赖选中项）。
    m_offlineNameEdit->setEnabled(idle);
    m_addOfflineButton->setEnabled(idle);
    m_hideCapeButton->setEnabled(idle);
    // 离线那一档：露着的时候才按 idle 判；「恢复默认外观」还要**确实有东西可撤** ——
    // 没有手动挑过、按下去什么也不会变的按钮，灰着比亮着好懂。
    m_offlineSkinButton->setEnabled(idle && m_offlineSkin);
    m_offlineSkinResetButton->setEnabled(idle && m_offlineSkin && m_hasCustomSkin);
}
