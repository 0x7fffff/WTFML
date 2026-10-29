/**
 * @file JavaMismatchDialog.cpp
 * @brief 实现实例 Java 与版本要求不匹配时的三岔口对话框。
 * @note 正文必须同时显示实例名和两个 Java 版本。
 */

#include "JavaMismatchDialog.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// 正文那个宽度常量（`promptContentWidth()`）是那一层给的 —— 这个框现在开在它里面。
#include "OverlayHost.hpp"
#include "Theme.hpp"   // 「取消」那一颗的身份（一般性 = 黑字黑边）

namespace {

/// 实际那个 Java 的可读写法。版本串可能是空的（release 快路径不回填 versionOutput），
/// 所以这里必须兜住"只有主版本号"这一种，否则正文里会出现「Java 」。
QString describeActual(int actualMajor, const QString &actualVersionText) {
    const QString text = actualVersionText.trimmed();
    if (text.isEmpty()) {
        return QStringLiteral("Java %1").arg(actualMajor);
    }
    return QStringLiteral("Java %1（%2）").arg(actualMajor).arg(text);
}

}  // namespace

QString JavaMismatchDialog::messageText(const QString &instanceName, int expectedMajor,
                                        int actualMajor, const QString &actualVersionText,
                                        const QString &javaPath) {
    return QStringLiteral(
               "实例「%1」在「实例设置…」里指定了 %2，而这个版本要求 Java %3。\n\n"
               "用它启动多半会失败。改用匹配的 Java 会把实例设置里那一项换成自动挑中的版本。\n\n"
               "%4")
        .arg(instanceName, describeActual(actualMajor, actualVersionText),
             QString::number(expectedMajor), javaPath);
}

JavaMismatchDialog::JavaMismatchDialog(const QString &instanceName, int expectedMajor,
                                       int actualMajor, const QString &actualVersionText,
                                       const QString &javaPath, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Java 版本对不上"));

    auto *layout = new QVBoxLayout(this);
    // 留白由卡片自己给（`promptPadding()`），这里再给一层就成了双层留白。
    layout->setContentsMargins(0, 0, 0, 0);

    m_textLabel = new QLabel(
        messageText(instanceName, expectedMajor, actualMajor, actualVersionText, javaPath), this);
    m_textLabel->setObjectName(textLabelName());
    m_textLabel->setWordWrap(true);
    // 那条路径会很长（`C:/.../.minecraft/runtime/.../bin/java.exe`），允许选中复制：
    // 用户要按正文里那句话去改配置时，手抄一条路径是不现实的。
    m_textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_textLabel->setFixedWidth(OverlayHost::promptContentWidth());
    layout->addWidget(m_textLabel);

    m_dontRemindCheck = new QCheckBox(tr("以后不再提醒这个实例"), this);
    m_dontRemindCheck->setObjectName(dontRemindCheckName());
    m_dontRemindCheck->setToolTip(
        tr("记在这个实例的设置里，可随时在「高级选项 → 版本提醒」改回来。"));
    layout->addWidget(m_dontRemindCheck);

    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    m_continueButton = new QPushButton(tr("仍然用这个 Java 启动"), this);
    m_continueButton->setObjectName(continueButtonName());
    // 默认按钮 = "什么都不改，照我钉的那个启动"。理由见 .hpp：一次误按回车不该改配置。
    m_continueButton->setDefault(true);
    m_useMatchingButton = new QPushButton(tr("改用匹配的 Java 启动"), this);
    m_useMatchingButton->setObjectName(useMatchingButtonName());
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_cancelButton->setObjectName(cancelButtonName());
    // 两颗「…启动」是确认性（绿），「取消」是一般性（黑）。
    Theme::markButton(m_cancelButton, Theme::ButtonKind::Neutral);
    m_cancelButton->setToolTip(tr("不启动。想先去把这个 Java 装好、或者改完实例设置再回来。"));
    buttons->addWidget(m_continueButton);
    buttons->addWidget(m_useMatchingButton);
    buttons->addStretch(1);
    buttons->addWidget(m_cancelButton);
    layout->addLayout(buttons);

    connect(m_continueButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::UseInstanceJava;
        accept();
    });
    connect(m_useMatchingButton, &QPushButton::clicked, this, [this] {
        m_choice = Choice::UseMatchingJava;
        accept();
    });
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    // 关窗 / Esc 走 reject()，**不动 m_choice** —— 它初值就是 Abort，那正是想要的。
}

bool JavaMismatchDialog::dontRemindAgain() const {
    // 构造失败（理论上到不了）时按"不勾"算：宁可下次再问一句，也不要静默地
    // 替用户把一个"以后别问了"的表态记下来。
    return m_dontRemindCheck != nullptr && m_dontRemindCheck->isChecked();
}
