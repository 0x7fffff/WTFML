/**
 * @file NewServerPage.cpp
 * @brief 「新建服务端」那一页的实现（形状与取舍见头文件）。
 */

#include "NewServerPage.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "MainWindowUI.hpp"
#include "Theme.hpp"

namespace {

/// 一行小字（说明/计划）。和更新中心那一页的 `makeMutedLabel()` 是同一套做法。
QLabel *makeMutedLabel(QWidget *parent, const QString &objectName) {
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    QPalette palette = label->palette();
    QColor muted = palette.color(QPalette::WindowText);
    muted.setAlpha(150);
    palette.setColor(QPalette::WindowText, muted);
    label->setPalette(palette);
    return label;
}

/// 一个"标签 + 输入"的行（**整行是一个控件**，好一起露/收）。返回那一行的容器，
/// `*editOut` 里回那个输入框。
QWidget *makeRow(QWidget *parent, const QString &labelText, QLineEdit **editOut,
                 const QString &objectName) {
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *label = new QLabel(labelText, row);
    label->setFixedWidth(96);
    layout->addWidget(label);
    auto *edit = new QLineEdit(row);
    edit->setObjectName(objectName);
    layout->addWidget(edit, 1);
    *editOut = edit;
    return row;
}

}  // namespace

NewServerPage::NewServerPage(QWidget *parent) : QDialog(parent) {
    setObjectName(MainWindowUI::newServerPageName());
    setWindowTitle(windowTitle());
    // 只影响"单独拿出来用"（验证工程那几处）：嵌进覆盖层时这一页拿的是内容区的整幅宽。
    resize(720, 520);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(12, 12, 12, 12);
    box->setSpacing(10);

    // ---------- 名称 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *label = new QLabel(QStringLiteral("名称"), row);
        label->setFixedWidth(96);
        layout->addWidget(label);
        m_nameEdit = new QLineEdit(row);
        m_nameEdit->setObjectName(MainWindowUI::newServerNameEditName());
        m_nameEdit->setPlaceholderText(QStringLiteral("会当成这个服务端的目录名"));
        layout->addWidget(m_nameEdit, 1);
        box->addWidget(row);
    }

    // ---------- 目录（只读：根只有一份，一个服务端 = 根下面一个目录）----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *label = new QLabel(QStringLiteral("目录"), row);
        label->setFixedWidth(96);
        layout->addWidget(label);
        m_targetDirLabel = makeMutedLabel(row, MainWindowUI::newServerTargetDirName());
        m_targetDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // 路径可能很长：**不许**参与横向度量（同一条规矩，和摘要那一句一样）。
        m_targetDirLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_targetDirLabel->setToolTip(QStringLiteral("服务端根目录下面以它的名字命名的那个目录"));
        layout->addWidget(m_targetDirLabel, 1);
        box->addWidget(row);
    }

    // ---------- 服务端类型 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *label = new QLabel(QStringLiteral("服务端类型"), row);
        label->setFixedWidth(96);
        layout->addWidget(label);
        m_flavorCombo = new QComboBox(row);
        m_flavorCombo->setObjectName(MainWindowUI::newServerFlavorComboName());
        layout->addWidget(m_flavorCombo, 1);
        box->addWidget(row);
    }

    // ---------- 游戏版本 ----------
    box->addWidget(makeRow(this, QStringLiteral("游戏版本"), &m_gameVersionEdit,
                           MainWindowUI::newServerGameVersionEditName()));
    // ---------- 加载器版本（按档露/收）----------
    m_loaderRow = makeRow(this, QStringLiteral("加载器版本"), &m_loaderEdit,
                          MainWindowUI::newServerLoaderEditName());
    box->addWidget(m_loaderRow);
    // ---------- 安装器版本（按档露/收）----------
    m_installerRow = makeRow(this, QStringLiteral("安装器版本"), &m_installerEdit,
                             MainWindowUI::newServerInstallerEditName());
    box->addWidget(m_installerRow);
    // ---------- 构建号（按档露/收）----------
    m_buildRow = makeRow(this, QStringLiteral("构建号"), &m_buildEdit,
                         MainWindowUI::newServerBuildEditName());
    box->addWidget(m_buildRow);

    // ---------- 内存 ----------
    {
        auto *row = new QWidget(this);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        auto *label = new QLabel(QStringLiteral("内存上限"), row);
        label->setFixedWidth(96);
        layout->addWidget(label);
        m_memorySpin = new QSpinBox(row);
        m_memorySpin->setObjectName(MainWindowUI::newServerMemorySpinName());
        m_memorySpin->setRange(512, 65536);
        m_memorySpin->setSingleStep(512);
        m_memorySpin->setValue(2048);
        m_memorySpin->setSuffix(QStringLiteral(" MB"));
        layout->addWidget(m_memorySpin);
        layout->addStretch(1);
        box->addWidget(row);
    }

    // ---------- 计划说明（由 MainWindow 现算现喂）----------
    m_summaryLabel = makeMutedLabel(this, MainWindowUI::newServerSummaryLabelName());
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // 折行 QLabel 的 sizeHint 是"整句排成一行"的宽度 —— 纯说明不许参与横向度量（同一条规矩）。
    m_summaryLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    box->addWidget(m_summaryLabel);
    box->addStretch(1);

    // ---------- 底下那两颗 ----------
    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto *cancel = new QPushButton(QStringLiteral("取消"), this);
    Theme::markButton(cancel, Theme::ButtonKind::Neutral);
    buttons->addWidget(cancel);
    m_createButton = new QPushButton(QStringLiteral("创建"), this);
    m_createButton->setObjectName(MainWindowUI::newServerCreateButtonName());
    Theme::markButton(m_createButton, Theme::ButtonKind::Accent);
    buttons->addWidget(m_createButton);
    box->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_createButton, &QPushButton::clicked, this, &QDialog::accept);

    // 用户改任何一格 → 告诉翻译点重算计划说明。
    const auto emitChanged = [this] { emit fieldsChanged(); };
    connect(m_nameEdit, &QLineEdit::textChanged, this, emitChanged);
    connect(m_gameVersionEdit, &QLineEdit::textChanged, this, emitChanged);
    connect(m_loaderEdit, &QLineEdit::textChanged, this, emitChanged);
    connect(m_installerEdit, &QLineEdit::textChanged, this, emitChanged);
    connect(m_buildEdit, &QLineEdit::textChanged, this, emitChanged);
    connect(m_memorySpin, &QSpinBox::valueChanged, this, emitChanged);
    connect(m_flavorCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyFlavorVisibility();
        emit fieldsChanged();
    });
}

void NewServerPage::setFlavors(const QList<FlavorOption> &flavors, const QString &defaultToken) {
    m_flavors = flavors;
    const QSignalBlocker blocker(m_flavorCombo);  // 灌数据这一步不算"用户改了"
    m_flavorCombo->clear();
    int defaultIndex = 0;
    for (int i = 0; i < flavors.size(); ++i) {
        const FlavorOption &option = flavors.at(i);
        m_flavorCombo->addItem(option.name);
        m_flavorCombo->setItemData(i, option.note, Qt::ToolTipRole);
        m_flavorCombo->setItemData(i, option.token, Qt::UserRole);
        if (option.token == defaultToken) {
            defaultIndex = i;
        }
    }
    m_flavorCombo->setCurrentIndex(defaultIndex);
    applyFlavorVisibility();
}

void NewServerPage::setDefaults(const QString &name, const QString &gameVersion) {
    const QSignalBlocker b1(m_nameEdit);
    const QSignalBlocker b2(m_gameVersionEdit);
    m_nameEdit->setText(name);
    m_gameVersionEdit->setText(gameVersion);
}

void NewServerPage::setTargetDir(const QString &dir) {
    if (m_targetDirLabel->text() == dir) {
        return;
    }
    m_targetDirLabel->setText(dir);
    m_targetDirLabel->setToolTip(dir.isEmpty()
                                     ? QStringLiteral("先给它起个名字，目录名是照着名字来的。")
                                     : dir);
}

void NewServerPage::setSummary(const QString &text, const QString &problem) {
    // 有问题就先说问题（那是用户现在要解决的事），没有才说计划。两句都是翻译点给的。
    const QString shown = problem.isEmpty() ? text : problem;
    if (m_summaryLabel->text() == shown) {
        return;
    }
    m_summaryLabel->setText(shown);
    // 有问题时「创建」点不动（填法不成立就点下去，只会建一个半残的服务端）。
    m_createButton->setEnabled(problem.isEmpty());
}

void NewServerPage::applyFlavorVisibility() {
    const int index = m_flavorCombo->currentIndex();
    const bool valid = index >= 0 && index < m_flavors.size();
    const FlavorOption option = valid ? m_flavors.at(index) : FlavorOption{};
    m_loaderRow->setVisible(option.needsLoader);
    m_installerRow->setVisible(option.needsInstaller);
    m_buildRow->setVisible(option.needsBuild);
}

QString NewServerPage::serverName() const { return m_nameEdit->text().trimmed(); }

QString NewServerPage::flavorToken() const {
    return m_flavorCombo->currentData(Qt::UserRole).toString();
}

QString NewServerPage::gameVersion() const { return m_gameVersionEdit->text().trimmed(); }

QString NewServerPage::loaderVersion() const {
    // 收着的那一行不算数（换档之后旧的字还留在框里，读回去就成了"这一档也有加载器版本"）。
    return m_loaderRow->isVisible() ? m_loaderEdit->text().trimmed() : QString();
}

QString NewServerPage::installerVersion() const {
    return m_installerRow->isVisible() ? m_installerEdit->text().trimmed() : QString();
}

QString NewServerPage::buildId() const {
    return m_buildRow->isVisible() ? m_buildEdit->text().trimmed() : QString();
}

int NewServerPage::memoryMb() const { return m_memorySpin->value(); }
